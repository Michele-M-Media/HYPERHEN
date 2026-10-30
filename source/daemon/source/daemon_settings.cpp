/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-local settings store + dual-path mtime reload gate.
 */

#include "daemon_ops.hpp"
#include "globalconf.hpp"
#include "hyper_runtime.hpp"
#include <onion/platform.h>
#include <onion/notify_i18n.h>
#include <onion/settings.hpp>
#include <onion/log_settings.hpp>
#include <mutex>

extern "C" int sceSystemServiceParamGetInt(int param_id, int *value);

namespace {

struct ConfigState {
  /** Max mtime of twin paths at last successful store update. */
  time_t last_modified = 0;
  bool ever_loaded = false;
};

ConfigState config_state;
std::mutex config_mutex;

void note_disk_written_locked() {
  config_state.last_modified = onion::settings_config_newest_mtime();
  config_state.ever_loaded = true;
}

int current_system_language() {
  int language = -1;
  if (sceSystemServiceParamGetInt(1, &language) < 0) language = -1;
  return language;
}

bool apply_settings_locked(const onion::Settings &requested,
                            const onion::Settings &previous,
                            int system_language) {
  onion::Settings s = requested;
  const bool selection_ok = onion::daemon::hyper::set_selection(s.hyper_model_id, s.hyper_voice_id);
  if (!selection_ok) {
    const auto accepted = onion::daemon::hyper::selection();
    s.hyper_model_id = accepted.model_id;
    s.hyper_voice_id = accepted.voice_id;
    // A failed active-model selection must not remain persisted while the
    // daemon continues using the previous model. Other changes, including
    // OFF and the independent download target, are still applied.
    if (!onion::settings_save(s)) LOG_ERROR("Hyper selection rollback could not be persisted");
    LOG_WARN("Hyper selection rejected model=%d voice=%d", requested.hyper_model_id,
             requested.hyper_voice_id);
  }
  const onion_log_level effective = onion::apply_log_settings(s);
  if (effective != static_cast<onion_log_level>(s.log_level)) {
    LOG_WARN("[Daemon] log level '%s' unavailable in this build; using '%s'",
             onion_log_level_name(static_cast<onion_log_level>(s.log_level)),
             onion_log_level_name(effective));
  }
  g_settings.store(s);
  onion::daemon::hyper::set_enabled(s.hyper_assistant_enabled);
  app_jailbreak_set_enabled(s.app_jailbreak_enabled);
  if (s.enable_fan_speed) {
    (void)set_fan_threshold(s.fan_threshold);
  } else if (config_state.ever_loaded && previous.enable_fan_speed) {
    (void)restore_automatic_fan();
  }
  onion_notify_apply_ui_language(s.ui_lang, system_language);
  return selection_ok;
}

} // namespace

onion::SettingsStore g_settings;

bool LoadSettings(bool force) {
  // Read, parse, publish and apply form one transaction. Otherwise an FPS
  // reload that captured ON could apply after a newer IPC reload applied OFF.
  std::lock_guard<std::mutex> guard(config_mutex);
  const onion::Settings previous = g_settings.snapshot();
  const time_t newest = onion::settings_config_newest_mtime();

  // Skip disk I/O when neither twin is newer than the last applied snapshot.
  if (!force && config_state.ever_loaded &&
      !onion::settings_config_is_newer_than(config_state.last_modified)) {
    return true;
  }

  const int system_language = current_system_language();
  if (system_language >= 0)
    onion_notify_set_language(onion_notify_resolve_language(0, system_language));

  if (newest == 0) {
    LOG_ERROR("[Daemon] Config file not found. Creating default schema...");
    if (onion::settings_ensure_default()) {
      onion_notify(true, "notify.settings.created");
    }
  }

  LOG_INFO("[Daemon] Loading Settings from shared schema...");
  onion::Settings s{};
  const bool from_file = onion::settings_load(&s);
  if (!from_file && newest != 0) {
    onion_notify(true, "notify.settings.read_failed");
    return false;
  }

  if (from_file) {
    LOG_INFO("[Daemon] Reading Settings from %s",
             onion::settings_last_loaded_path());
  } else {
    LOG_INFO("[Daemon] Using default settings (no config file)");
  }
  LOG_DEBUG("fan_threshold: %d", s.fan_threshold);
  LOG_DEBUG("enable_fan_speed: %d", s.enable_fan_speed ? 1 : 0);

  /* Apply before the rest: a reload that raises the level should take effect
     for the messages that follow it in this same call. */
  const bool applied = apply_settings_locked(s, previous, system_language);
  note_disk_written_locked();
  return applied;
}

void SettingsNoteDiskWritten() {
  std::lock_guard<std::mutex> guard(config_mutex);
  note_disk_written_locked();
}

bool SettingsSetFanControl(bool enabled, int threshold) {
  std::lock_guard<std::mutex> guard(config_mutex);
  const onion::Settings previous = g_settings.snapshot();
  onion::Settings saved = previous;
  onion::Settings disk{};
  if (onion::settings_load(&disk)) saved = disk;
  else if (onion::settings_config_newest_mtime() != 0) return false;
  saved.enable_fan_speed = enabled;
  if (enabled) saved.fan_threshold = threshold;
  const bool persisted = onion::settings_save(saved);
  const bool applied = apply_settings_locked(saved, previous, current_system_language());
  if (persisted) note_disk_written_locked();
  return persisted && applied;
}
