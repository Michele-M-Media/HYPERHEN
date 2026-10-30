/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress system settings domain */
#include "onpress.hpp"
#include "toolbox_i18n.hpp"
#include "hyper_p5_bridge.hpp"
#include <onion/hyperlinux_profiles.h>
#include <onion/hyper_model_catalog.hpp>
#include <onion/hyper_voice_catalog.hpp>
#include <onion/notify_i18n.h>
#include <cstdlib>
#include <cstring>
#include "onion_cjson.hpp"

namespace {
constexpr const char *kHyperLinuxBootMarker =
    "/user/data/HyperHEN/boot_linux_next";

// Hyper status/install commands return cached state or queue background work.
// Keep each exchange short and never change the shared client's timeout.
constexpr int kHyperIpcTimeoutMs = 250;

bool hyper_cached_model_ready(const std::string &response, int requested_id) {
  if (!onion::hyper::model_entry(requested_id)) return false;
  onion_cjson::Root root(response);
  if (!root || !cJSON_IsObject(root.get()) ||
      !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root.get(), "cache_ready")))
    return false;
  const auto *models = cJSON_GetObjectItemCaseSensitive(root.get(), "models");
  if (!cJSON_IsArray(models)) return false;
  bool seen[onion::hyper::kModelCatalogCount] {};
  bool requested_ready = false;
  size_t count = 0;
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, models) {
    if (++count > onion::hyper::kModelCatalogCount || !cJSON_IsObject(item))
      return false;
    const auto *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    const auto *ready = cJSON_GetObjectItemCaseSensitive(item, "ready");
    if (!cJSON_IsNumber(id) ||
        id->valuedouble != static_cast<double>(id->valueint) ||
        !onion::hyper::model_entry(id->valueint) ||
        (!cJSON_IsTrue(ready) && !cJSON_IsFalse(ready)) || seen[id->valueint])
      return false;
    seen[id->valueint] = true;
    if (id->valueint == requested_id) requested_ready = cJSON_IsTrue(ready);
  }
  return requested_ready;
}

bool hyper_model_ready(int id) {
  // SceShellUI uses /user/data; elevated builds may expose /data directly.
  // This checks only bounded markers and part sizes, never model hashes.
  if (onion::hyper::model_ready_fast(id, "/user/data/Hyper") ||
      onion::hyper::model_ready_fast(id, "/data/Hyper"))
    return true;
  // Markers may be unreadable in ShellUI's UID/jail even through /user/data.
  // Hyper's daemon service owns the catalog cache; this never scans or hashes
  // models on the UI thread, and every failed/malformed reply rejects closed.
  std::string response;
  return IPC_Client::getInstance(false).IPCSendCommand(
             BREW_UTIL_HYPER_CATALOG_STATUS, response, "{}", kHyperIpcTimeoutMs) &&
         hyper_cached_model_ready(response, id);
}

void hyper_bind_control(OnPressContext &ctx, const std::string &value) {
  MonoDomain *domain = mono_domain_get ? mono_domain_get() : Root_Domain;
  if (!domain) domain = Root_Domain;
  if (ctx.element && domain && set_value_method &&
      mono_string_new && mono_runtime_invoke) {
    MonoString *text = mono_string_new(domain, value.c_str());
    if (text) {
      void *args[] = {text};
      MonoObject *exception = nullptr;
      mono_runtime_invoke(set_value_method, ctx.element, args, &exception);
      if (exception) LOG_ERROR("Failed to bind Hyper assistant control value");
    }
  }
}

bool hyper_choice_id(const std::string &value, int *id) {
  if (!id || value.empty() || value.size() > 3) return false;
  int result = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') return false;
    result = result * 10 + c - '0';
  }
  *id = result;
  return true;
}

bool hyper_request(bool util, DaemonCommands command, std::string &response) {
  return IPC_Client::getInstance(util).IPCSendCommand(
      command, response, "{}", kHyperIpcTimeoutMs);
}

// These are the daemon's accepted selection IDs, not the worker's old model
// while a replacement starts. Require exact known integers before trusting
// cached status as the authority for a rejected/uncertain config transaction.
bool hyper_accepted_selection(const std::string &response, int *model_id,
                               int *voice_id) {
  onion_cjson::Root root(response);
  if (!model_id || !voice_id || !root || !cJSON_IsObject(root.get()))
    return false;
  const auto *model = cJSON_GetObjectItemCaseSensitive(root.get(), "model_id");
  const auto *voice = cJSON_GetObjectItemCaseSensitive(root.get(), "voice_id");
  if (!cJSON_IsNumber(model) || !cJSON_IsNumber(voice) ||
      model->valuedouble != static_cast<double>(model->valueint) ||
      voice->valuedouble != static_cast<double>(voice->valueint) ||
      !onion::hyper::model_entry(model->valueint) ||
      !onion::hyper::voice_entry(voice->valueint))
    return false;
  *model_id = model->valueint;
  *voice_id = voice->valueint;
  return true;
}

// HYPER_STATUS_PRESENTATION_BEGIN -- exercised by the host frontend test.
bool hyper_show_status(const std::string &response, bool installation) {
  onion_cjson::Root root(response);
  if (!root || !cJSON_IsObject(root.get()))
    return false;
  const char *state = onion_cjson::string_item(root.get(), "state");
  if (!state)
    return false;
  if (installation && std::strcmp(state, "error") == 0) {
    notify("notify.hyper.install_failed",
           onion_cjson::int_item(root.get(), "error_code", -1));
    return true;
  }
  if (std::strcmp(state, "downloading") == 0 ||
      std::strcmp(state, "installing") == 0) {
    int progress = onion_cjson::int_item(root.get(), "progress", 0);
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    notify(std::strcmp(state, "downloading") == 0
               ? "notify.hyper.status.downloading"
               : "notify.hyper.status.installing", progress);
    return true;
  }
  struct StateKey { const char *state; const char *key; };
  static constexpr StateKey states[] = {
      {"off", "notify.hyper.status.off"},
      {"missing", "notify.hyper.status.missing"},
      {"starting", "notify.hyper.status.starting"},
      {"ready", "notify.hyper.status.ready"},
      {"stopping", "notify.hyper.status.stopping"},
      {"error", "notify.hyper.status.error"},
      {"waiting_toolbox", "notify.hyper.status.waiting_toolbox"},
      {"verifying", "notify.hyper.status.verifying"},
      {"cancelled", "notify.hyper.status.cancelled"},
  };
  for (const auto &entry : states) {
    if (std::strcmp(state, entry.state) == 0) {
      notify(entry.key);
      return true;
    }
  }
  return false;
}

bool hyper_install_status_is_actionable(const std::string &response) {
  onion_cjson::Root root(response);
  const char *state = root ? onion_cjson::string_item(root.get(), "state")
                           : nullptr;
  return state && (std::strcmp(state, "verifying") == 0 ||
                   std::strcmp(state, "downloading") == 0 ||
                   std::strcmp(state, "installing") == 0 ||
                   std::strcmp(state, "error") == 0 ||
                   std::strcmp(state, "cancelled") == 0);
}
// HYPER_STATUS_PRESENTATION_END
}

static OnPressResult id_hyper_assistant_enabled(OnPressContext &ctx) {
  // This custom control owns persistence and Value binding. It never reaches
  // Sony's stock OnPressed and never commits the same setting a second time.
  ctx.dirty = false;
  const bool previous = g_settings.hyper_assistant_enabled;
  const bool valid = ctx.value == "0" || ctx.value == "1" ||
                     ctx.value == "false" || ctx.value == "true";
  const bool enabled = ctx.value == "1" || ctx.value == "true";
  if (valid && enabled != previous) {
    g_settings.hyper_assistant_enabled = enabled;
    if (!SaveSettings()) {
      g_settings.hyper_assistant_enabled = previous;
      notify("notify.hyper.settings_save_failed");
    } else {
      // Publish only a successfully persisted switch. OFF immediately closes
      // the voice guard even if the daemon's reload reply is delayed.
      hyper_p5_bridge_set_enabled(enabled);
      std::string response;
      if (!hyper_request(false, BREW_RELOAD_SETTINGS, response))
        notify("notify.hyper.status.unavailable");
    }
  }

  // Same managed setter used by the existing payload config controls.
  // Rebind the persisted value on success, rejected input and write failure.
  hyper_bind_control(ctx, g_settings.hyper_assistant_enabled ? "1" : "0");
  return OnPressResult::Consumed;
}

// One explicit save transaction for every catalog selection. Rebind the
// previous value on invalid input or failed persistence; reload only settings
// that affect the current assistant. The daemon owns model restart/voice apply.
static OnPressResult hyper_select_id(OnPressContext &ctx, int &setting,
                                     int selected, bool valid, bool reload) {
  ctx.dirty = false;
  const int previous = setting;
  const onion::Settings previous_settings = g_settings;
  if (valid && selected != previous) {
    setting = selected;
    if (!SaveSettings()) {
      setting = previous;
      notify("notify.hyper.settings_save_failed");
    } else if (reload) {
      std::string response;
      if (!hyper_request(false, BREW_RELOAD_SETTINGS, response)) {
        // Disk may still contain the rejected request if the daemon could
        // not persist its rollback. Its cached accepted IDs are authoritative.
        int accepted_model = 0, accepted_voice = 0;
        response.clear();
        if (hyper_request(false, BREW_HYPER_STATUS, response) &&
            hyper_accepted_selection(response, &accepted_model, &accepted_voice)) {
          g_settings.hyper_model_id = accepted_model;
          g_settings.hyper_voice_id = accepted_voice;
          // ShellUI has a different filesystem view and may be able to repair
          // persistence even when the daemon could not. Keep accepted UI values
          // if this write also fails, and explicitly report that failure.
          if (!SaveSettings())
            notify("notify.hyper.settings_save_failed");
          else
            notify("notify.hyper.status.unavailable");
        } else {
          // No confirmed runtime selection: restore the pre-press UI snapshot.
          // Re-reading disk here could reapply the rejected/pending request.
          g_settings = previous_settings;
          notify("notify.hyper.status.unavailable");
        }
      }
    }
  }
  hyper_bind_control(ctx, std::to_string(setting));
  return OnPressResult::Consumed;
}

static OnPressResult id_hyper_assistant_model(OnPressContext &ctx) {
  int selected = -1;
  const bool valid = hyper_choice_id(ctx.value, &selected) &&
                     onion::hyper::model_entry(selected);
  if (valid && selected != g_settings.hyper_model_id &&
      !hyper_model_ready(selected)) {
    ctx.dirty = false;
    hyper_bind_control(ctx, std::to_string(g_settings.hyper_model_id));
    notify("notify.hyper.model_missing");
    return OnPressResult::Consumed;
  }
  return hyper_select_id(ctx, g_settings.hyper_model_id, selected, valid, true);
}

static OnPressResult id_hyper_assistant_download_model(OnPressContext &ctx) {
  int selected = -1;
  const bool valid = hyper_choice_id(ctx.value, &selected) &&
                     onion::hyper::model_entry(selected);
  return hyper_select_id(ctx, g_settings.hyper_download_model_id, selected,
                         valid, false);
}

static OnPressResult id_hyper_assistant_voice(OnPressContext &ctx) {
  int selected = -1;
  const bool valid = hyper_choice_id(ctx.value, &selected) &&
                     onion::hyper::voice_entry(selected);
  return hyper_select_id(ctx, g_settings.hyper_voice_id, selected, valid, true);
}

static OnPressResult id_hyper_assistant_download(OnPressContext &ctx) {
  ctx.dirty = false;
  if (!onion::hyper::model_entry(g_settings.hyper_download_model_id))
    return OnPressResult::Consumed;
  const std::string body = "{\"model_id\":" +
      std::to_string(g_settings.hyper_download_model_id) + "}";
  std::string response;
  if (!IPC_Client::getInstance(false).IPCSendCommand(
          BREW_UTIL_HYPER_MODEL_DOWNLOAD, response, body, kHyperIpcTimeoutMs))
    notify("notify.hyper.status.unavailable");
  // Downloading never changes the selected active model.
  return OnPressResult::Consumed;
}

static OnPressResult id_hyper_assistant_models(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string response;
  if (!hyper_request(false, BREW_UTIL_HYPER_INSTALL, response))
    notify("notify.hyper.status.unavailable");
  // The installer owns acceptance/progress/failure notifications.
  return OnPressResult::Consumed;
}

static OnPressResult id_hyper_assistant_cancel(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string response;
  if (!hyper_request(false, BREW_UTIL_HYPER_INSTALL_CANCEL, response))
    notify("notify.hyper.status.unavailable");
  return OnPressResult::Consumed;
}

static OnPressResult id_hyper_assistant_status(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string response;
  // A completed installer must not hide the live on/off assistant state.
  if (hyper_request(false, BREW_UTIL_HYPER_INSTALL_STATUS, response) &&
      hyper_install_status_is_actionable(response) &&
      hyper_show_status(response, true))
    return OnPressResult::Consumed;
  response.clear();
  if (!hyper_request(false, BREW_HYPER_STATUS, response) ||
      !hyper_show_status(response, false))
    notify("notify.hyper.status.unavailable");
  return OnPressResult::Consumed;
}

static OnPressResult id_linux_loader(OnPressContext &ctx) {
  ctx.dirty = false;

  OrbisKernelSwVersion sw{};
  if (!sceKernelGetProsperoSystemSwVersion ||
      sceKernelGetProsperoSystemSwVersion(&sw) != 0 ||
      !onion_hyperlinux_firmware_supported(sw.version)) {
    notify("notify.linux.unsupported");
    return OnPressResult::Consumed;
  }

  if (!touch_file(kHyperLinuxBootMarker)) {
    notify("notify.linux.arm_failed");
    return OnPressResult::Consumed;
  }

  LOG_INFO("HyperLinux clean-boot mode armed for firmware 0x%08x",
           sw.version);
  notify("notify.linux.armed");
  return OnPressResult::Consumed;
}

static OnPressResult id_start_opt(OnPressContext &ctx) {
  char *end = nullptr;
  const long selected = std::strtol(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0' ||
      (selected != onion::kStartupOpenNone &&
       selected != onion::kStartupOpenHomeMenu)) {
    LOG_WARN("Rejected unsupported startup destination: %s",
             ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }

  const int destination = static_cast<int>(selected);
  if (destination == g_settings.startup_open_after_load) {
    return OnPressResult::EarlyReturn;
  }

  g_settings.startup_open_after_load = destination;
  LOG_INFO("Startup destination: %s",
           destination == onion::kStartupOpenHomeMenu ? "home_menu" : "none");
  return OnPressResult::Handled;
}

static OnPressResult id_log_level(OnPressContext &ctx) {
  char *end = nullptr;
  const long selected = std::strtol(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0' ||
      selected < onion::kLogLevelOff ||
      selected > static_cast<long>(ONION_LOG_COMPILE_LEVEL)) {
    LOG_WARN("Rejected unsupported log level: %s", ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }

  const int level = static_cast<int>(selected);
  if (level == g_settings.log_level) {
    return OnPressResult::EarlyReturn;
  }

  LOG_INFO("Changing log level from %s to %s",
           onion_log_level_name(
               static_cast<onion_log_level>(g_settings.log_level)),
           onion_log_level_name(static_cast<onion_log_level>(level)));
  g_settings.log_level = level;
  ctx.reload_main = true;
  ctx.reload_util = true;
  return OnPressResult::Handled;
}

static OnPressResult id_log_max_bytes(OnPressContext &ctx) {
  char *end = nullptr;
  const long long selected = std::strtoll(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0') {
    LOG_WARN("Rejected log file cap: %s", ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }

  /* The list only offers sane presets, but the cap is clamped anyway so a
     hand-edited config cannot arrive here out of range. */
  const int bytes = onion::clamp_log_max_bytes(selected);
  if (bytes == g_settings.log_max_bytes) {
    return OnPressResult::EarlyReturn;
  }

  /* Three backups are kept, so the retained history is four times the cap. */
  LOG_INFO("Log file cap: %d bytes (%d KiB per file, %d KiB retained)", bytes,
           bytes / 1024, bytes / 1024 * 4);
  g_settings.log_max_bytes = bytes;
  ctx.reload_main = true;
  ctx.reload_util = true;
  return OnPressResult::Handled;
}

static OnPressResult id_boot_thanks(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.show_boot_thanks) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.show_boot_thanks = enabled;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_hype_voice(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.hype_voice) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.hype_voice = enabled;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_debug_jb(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.debug_app_jb_msg) {
    LOG_WARN("Debug JB already %s",
                g_settings.debug_app_jb_msg ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.debug_app_jb_msg = !g_settings.debug_app_jb_msg;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_app_jailbreak_enabled(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.app_jailbreak_enabled) {
    LOG_WARN("App jailbreak listener already %s",
             enabled ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.app_jailbreak_enabled = enabled;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_custom_game_opts(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.hyperhen_game_opts) {
    LOG_WARN("HyperHEN Game Options already %s",
                g_settings.hyperhen_game_opts ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.hyperhen_game_opts = !g_settings.hyperhen_game_opts;
  LOG_DEBUG("HyperHEN Game Options: %s",
              g_settings.hyperhen_game_opts ? "Enabled" : "Disabled");
  return OnPressResult::Handled;
}

static OnPressResult id_ui_lang(OnPressContext &ctx) {
  int v = atoi(ctx.value.c_str());
  if (v < onion::kUiLanguageSystem || v > onion::kUiLanguageMax)
    v = onion::kUiLanguageSystem;
  if (v == g_settings.ui_lang)
    return OnPressResult::EarlyReturn;
  g_settings.ui_lang = v;
  LOG_DEBUG("UI language: %s", onion::language_name(v));
  toolbox_i18n::apply_system_or_ui_lang(v);
  /* Language is process-local. This only re-reads config.ini in daemon/util
     (LoadSettings); it does not inject or restart SceShellUI. Cheat toasts
     are sent by util, so it must apply the new language too. */
  ctx.reload_main = true;
  ctx.reload_util = true;
  /* XML is built when the page opens; current tree stays in the old language. */
  notify("notify.lang.saved");
  return OnPressResult::Handled;
}

static OnPressResult id_rest_1(OnPressContext &ctx) {
  char *end = nullptr;
  const long selected = std::strtol(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0' ||
      selected < onion::kRestModeDelayMinSeconds ||
      selected > onion::kRestModeDelayMaxSeconds) {
    LOG_WARN("Rejected Rest Mode delay: %s", ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }
  const uint64_t delay = static_cast<uint64_t>(selected);
  if (delay == g_settings.rest_mode_delay_seconds) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.rest_mode_delay_seconds = delay;
  ctx.reload_main = true;
  LOG_INFO("Rest Mode toolbox delay: %llu s",
           static_cast<unsigned long long>(delay));
  return OnPressResult::Handled;
}

static OnPressResult id_enable_fan_speed(OnPressContext &ctx) {
  if (atol(ctx.value.c_str()) == g_settings.enable_fan_speed) {
    LOG_WARN("Fan speed control already %s",
                g_settings.enable_fan_speed ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  g_settings.enable_fan_speed = !g_settings.enable_fan_speed;
  IPC_Client::getInstance(false).Set_Fan_Threshold(g_settings.fan_threshold,
                                                   g_settings.enable_fan_speed);
  return OnPressResult::Handled;
}

static OnPressResult id_fan_speed(OnPressContext &ctx) {
  int &fan_speed = g_settings.fan_threshold;
  fan_speed = atoi(ctx.value.c_str());
  if (!g_settings.enable_fan_speed) {
    notify("notify.fan.not_enabled");
    return OnPressResult::EarlyReturn;
  }
  LOG_DEBUG("Setting fan speed to %d%%", fan_speed);
  IPC_Client::getInstance(false).Set_Fan_Threshold(fan_speed,
                                                   g_settings.enable_fan_speed);
  return OnPressResult::Handled;
}

static OnPressResult id_cheats_mirror(OnPressContext &ctx) {
  char *end = nullptr;
  const long selected = std::strtol(ctx.value.c_str(), &end, 10);
  if (end == ctx.value.c_str() || *end != '\0' ||
      (selected != onion::kCheatsMirrorAuto &&
       selected != onion::kCheatsMirrorGithub &&
       selected != onion::kCheatsMirrorCnb)) {
    LOG_WARN("Rejected unsupported cheats mirror: %s", ctx.value.c_str());
    return OnPressResult::EarlyReturn;
  }
  const int mirror = static_cast<int>(selected);
  if (mirror == g_settings.cheats_mirror) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.cheats_mirror = mirror;
  ctx.reload_util = true;
  LOG_INFO("Cheats mirror: %d", mirror);
  return OnPressResult::Handled;
}

static OnPressResult id_cheats_shortcut(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.cheats_shortcut_opt) {
    LOG_WARN("Cheats_shortcut already %i", g_settings.cheats_shortcut_opt);
    return OnPressResult::EarlyReturn;
  }
  Cheats_Shortcut opt = (Cheats_Shortcut)atoi(ctx.value.c_str());
  if (opt == CHEATS_SINGLE_SHARE) {
    if (g_settings.toolbox_shortcut_opt == TOOLBOX_SINGLE_SHARE) {
      notify("notify.shortcut.toolbox_cheats");
      return OnPressResult::EarlyReturn;
    }
  } else if (opt == CHEATS_LONG_SHARE) {
    if (g_settings.toolbox_shortcut_opt == TOOLBOX_LONG_SHARE) {
      notify("notify.shortcut.toolbox_cheats_long");
      return OnPressResult::EarlyReturn;
    }
  }
  g_settings.cheats_shortcut_opt = static_cast<int>(opt);
  return OnPressResult::Handled;
}

static OnPressResult id_toolbox_shortcut(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.toolbox_shortcut_opt) {
    LOG_WARN("toolbox_shortcut_opt already %i",
                g_settings.toolbox_shortcut_opt);
    return OnPressResult::EarlyReturn;
  }
  Toolbox_Shortcut opt = (Toolbox_Shortcut)atoi(ctx.value.c_str());
  if (opt == TOOLBOX_SINGLE_SHARE) {
    if (g_settings.cheats_shortcut_opt == CHEATS_SINGLE_SHARE) {
      notify("notify.shortcut.cheats_toolbox");
      return OnPressResult::EarlyReturn;
    }
  } else if (opt == TOOLBOX_LONG_SHARE) {
    if (g_settings.cheats_shortcut_opt == CHEATS_LONG_SHARE) {
      notify("notify.shortcut.cheats_toolbox_long");
      return OnPressResult::EarlyReturn;
    }
  }
  g_settings.toolbox_shortcut_opt = static_cast<int>(opt);
  return OnPressResult::Handled;
}

static const OnPressExactEntry kExact[] = {
    {"id_hyper_assistant_enabled", id_hyper_assistant_enabled},
    {"id_hyper_assistant_model", id_hyper_assistant_model},
    {"id_hyper_assistant_download_model", id_hyper_assistant_download_model},
    {"id_hyper_assistant_voice", id_hyper_assistant_voice},
    {"id_hyper_assistant_download", id_hyper_assistant_download},
    {"id_hyper_assistant_models", id_hyper_assistant_models},
    {"id_hyper_assistant_cancel", id_hyper_assistant_cancel},
    {"id_hyper_assistant_status", id_hyper_assistant_status},
    {"id_linux_loader", id_linux_loader},
    {"id_start_opt", id_start_opt},
    {"id_log_level", id_log_level},
    {"id_log_max_bytes", id_log_max_bytes},
    {"id_boot_thanks", id_boot_thanks},
    {"id_hype_voice", id_hype_voice},
    {"id_app_jailbreak_enabled", id_app_jailbreak_enabled},
    {"id_debug_jb", id_debug_jb},
    {"id_custom_game_opts", id_custom_game_opts},
    {"id_ui_lang", id_ui_lang},
    {"id_rest_1", id_rest_1},
    {"id_enable_fan_speed", id_enable_fan_speed},
    {"id_fan_speed", id_fan_speed},
    {"id_cheats_mirror", id_cheats_mirror},
    {"id_cheats_shortcut", id_cheats_shortcut},
    {"id_toolbox_shortcut", id_toolbox_shortcut},
};

const OnPressExactEntry *onpress_system_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
