/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-local settings store + dual-path mtime reload gate.
 */

#include "daemon_ops.hpp"
#include "globalconf.hpp"
#include <onion/platform.h>
#include <onion/notify_i18n.h>
#include <onion/settings.hpp>
#include <onion/log_settings.hpp>

extern "C" int sceSystemServiceParamGetInt(int param_id, int *value);

namespace {

struct ConfigState {
  /** Max mtime of twin paths at last successful store update. */
  time_t last_modified = 0;
  bool ever_loaded = false;
};

ConfigState config_state;

} // namespace

onion::SettingsStore g_settings;

bool LoadSettings(bool force) {
  const onion::Settings previous = g_settings.snapshot();
  const time_t newest = onion::settings_config_newest_mtime();

  // Skip disk I/O when neither twin is newer than the last applied snapshot.
  if (!force && config_state.ever_loaded &&
      !onion::settings_config_is_newer_than(config_state.last_modified)) {
    return true;
  }

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
  const onion_log_level effective = onion::apply_log_settings(s);
  if (effective != static_cast<onion_log_level>(s.log_level)) {
    LOG_WARN("[Daemon] log level '%s' unavailable in this build; using '%s'",
                 onion_log_level_name(static_cast<onion_log_level>(s.log_level)),
                 onion_log_level_name(effective));
  }

  g_settings.store(s);
  app_jailbreak_set_enabled(s.app_jailbreak_enabled);

  /* Immediate apply on config change. fan_maintenance_thread keeps rewriting
     the threshold so firmware cannot silently restore its own curve. */
  if (s.enable_fan_speed) {
    (void)set_fan_threshold(s.fan_threshold);
  } else if (config_state.ever_loaded && previous.enable_fan_speed) {
    (void)restore_automatic_fan();
  }

  int system_language = 1;
  if (s.ui_lang == onion::kUiLanguageSystem)
    (void)sceSystemServiceParamGetInt(1, &system_language);
  onion_notify_apply_ui_language(s.ui_lang, system_language);
  config_state.last_modified = onion::settings_config_newest_mtime();
  config_state.ever_loaded = true;
  return true;
}

void SettingsNoteDiskWritten() {
  config_state.last_modified = onion::settings_config_newest_mtime();
  config_state.ever_loaded = true;
}
