#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cr_config.h"
#include "cr_hotkey_buttons.h"
#include "cr_log.h"

static time_t g_cfg_mtime = 0;

pthread_mutex_t g_cfg_lock = PTHREAD_MUTEX_INITIALIZER;

cheatrunner_config_t g_cfg = {
    .http_port = CHEATRUNNER_HTTP_PORT,
    .auto_load_cheat_menu = 1,
    .auto_download_missing_cheat = 0,
    .launch_kill_current = 1,
    .launch_kill_delay_ms = 1000,
    .launch_wait_timeout_ms = 60000,
    .cheat_engine = 1,
    .cheat_restore_rx = 0,
    .cheat_restore_original_prot = 1,
    .allow_force_enable = 0,
    .cheat_state_after_launch_delay_ms = 8000,
    .launch_post_timeout_grace_ms = 3000,
    .dev_reload_enabled = 1,
    .dev_shutdown_delay_ms = 700,
    .theme = "midnight",
    .launch_user_id = "auto",
    .cheat_post_apply_watch_ms = 8000,
    .cheat_mark_crash_suspect = 1,
    .cheat_apply_one_at_a_time = 1,
    .cheat_apply_cooldown_ms = 500,
    .cheat_post_apply_poll_ms = 500,
    .cheat_min_stable_ms = 8000,
    .cheat_sources_enabled = 1,
    .cheat_remote_download_enabled = 1,
    .cheat_source_cache_ttl_seconds = 21600,
    .cheat_remote_max_file_bytes = 1048576,
    .title_lookup_enabled = 1,
    .title_lookup_cache_enabled = 1,
    .title_lookup_timeout_ms = 8000,
    .games_cache_ttl_ms = 30000,
    .appdb_debug_names = 0,
    .log_level = "debug",
    /* On by default: only engages for MC-dependent mods, so working cheats are unaffected. */
    .cheat_master_code_fixup = 1,
    .cheat_inter_mod_delay_ms = 0,
    .tile_autoinstall_enabled = 1,
    .hotkey_enabled = 0,
    .hotkey_button_a = "L2",
    .hotkey_button_b = "R3",
    .hotkey_hold_ms = 800,
};

void
config_set_defaults(cheatrunner_config_t *cfg) {
  if (!cfg) {
    return;
  }
  *cfg = (cheatrunner_config_t){
      .http_port = CHEATRUNNER_HTTP_PORT,
      .auto_load_cheat_menu = 1,
      .auto_download_missing_cheat = 0,
      .launch_kill_current = 1,
      .launch_kill_delay_ms = 1000,
      .launch_wait_timeout_ms = 60000,
      .cheat_engine = 1,
      .cheat_restore_rx = 0,
      .cheat_restore_original_prot = 1,
      .allow_force_enable = 0,
      .cheat_state_after_launch_delay_ms = 8000,
      .launch_post_timeout_grace_ms = 3000,
      .dev_reload_enabled = 1,
      .dev_shutdown_delay_ms = 700,
      .theme = "midnight",
      .launch_user_id = "auto",
      .cheat_post_apply_watch_ms = 8000,
      .cheat_mark_crash_suspect = 1,
      .cheat_apply_one_at_a_time = 1,
      .cheat_apply_cooldown_ms = 500,
      .cheat_post_apply_poll_ms = 500,
      .cheat_min_stable_ms = 8000,
      .cheat_sources_enabled = 1,
      .cheat_remote_download_enabled = 1,
      .cheat_source_cache_ttl_seconds = 21600,
      .cheat_remote_max_file_bytes = 1048576,
      .title_lookup_enabled = 1,
      .title_lookup_cache_enabled = 1,
      .title_lookup_timeout_ms = 8000,
      .games_cache_ttl_ms = 30000,
      .appdb_debug_names = 0,
      .log_level = "debug",
      .cheat_master_code_fixup = 0,
      .cheat_inter_mod_delay_ms = 0,
      .tile_autoinstall_enabled = 1,
      .hotkey_enabled = 0,
      .hotkey_button_a = "L2",
      .hotkey_button_b = "R3",
      .hotkey_hold_ms = 800,
  };
}

int
config_save_locked(void) {
  char txt[5120];
  int n = snprintf(
      txt, sizeof(txt),
      "http_port=%d\n"
      "auto_load_cheat_menu=%d\n"
      "auto_download_missing_cheat=%d\n"
      "launch_kill_current=%d\n"
      "launch_kill_delay_ms=%d\n"
      "launch_wait_timeout_ms=%d\n"
      "cheat_engine=%d\n"
      "cheat_restore_rx=%d\n"
      "cheat_restore_original_prot=%d\n"
      "allow_force_enable=%d\n"
      "cheat_state_after_launch_delay_ms=%d\n"
      "launch_post_timeout_grace_ms=%d\n"
      "launch_user_id=%s\n"
      "dev_reload_enabled=%d\n"
      "dev_shutdown_delay_ms=%d\n"
      "cheat_post_apply_watch_ms=%d\n"
      "cheat_mark_crash_suspect=%d\n"
      "cheat_apply_one_at_a_time=%d\n"
      "cheat_apply_cooldown_ms=%d\n"
      "cheat_post_apply_poll_ms=%d\n"
      "cheat_min_stable_ms=%d\n"
      "cheat_sources_enabled=%d\n"
      "cheat_remote_download_enabled=%d\n"
      "cheat_source_cache_ttl_seconds=%d\n"
      "cheat_remote_max_file_bytes=%d\n"
      "title_lookup_enabled=%d\n"
      "title_lookup_cache_enabled=%d\n"
      "title_lookup_timeout_ms=%d\n"
      "games_cache_ttl_ms=%d\n"
      "appdb_debug_names=%d\n"
      "log_level=%s\n"
      "cheat_master_code_fixup=%d\n"
      "cheat_inter_mod_delay_ms=%d\n"
      "tile_autoinstall_enabled=%d\n"
      "hotkey_enabled=%d\n"
      "hotkey_button_a=%s\n"
      "hotkey_button_b=%s\n"
      "hotkey_hold_ms=%d\n"
      "theme=%s\n",
      g_cfg.http_port, g_cfg.auto_load_cheat_menu,
      g_cfg.auto_download_missing_cheat, g_cfg.launch_kill_current,
      g_cfg.launch_kill_delay_ms, g_cfg.launch_wait_timeout_ms,
      g_cfg.cheat_engine,
      g_cfg.cheat_restore_rx, g_cfg.cheat_restore_original_prot,
      g_cfg.allow_force_enable,
      g_cfg.cheat_state_after_launch_delay_ms, g_cfg.launch_post_timeout_grace_ms,
      g_cfg.launch_user_id,
      g_cfg.dev_reload_enabled, g_cfg.dev_shutdown_delay_ms,
      g_cfg.cheat_post_apply_watch_ms, g_cfg.cheat_mark_crash_suspect,
      g_cfg.cheat_apply_one_at_a_time, g_cfg.cheat_apply_cooldown_ms,
      g_cfg.cheat_post_apply_poll_ms,
      g_cfg.cheat_min_stable_ms,
      g_cfg.cheat_sources_enabled, g_cfg.cheat_remote_download_enabled,
      g_cfg.cheat_source_cache_ttl_seconds, g_cfg.cheat_remote_max_file_bytes,
      g_cfg.title_lookup_enabled, g_cfg.title_lookup_cache_enabled, g_cfg.title_lookup_timeout_ms,
      g_cfg.games_cache_ttl_ms, g_cfg.appdb_debug_names, g_cfg.log_level,
      g_cfg.cheat_master_code_fixup,
      g_cfg.cheat_inter_mod_delay_ms,
      g_cfg.tile_autoinstall_enabled,
      g_cfg.hotkey_enabled, g_cfg.hotkey_button_a, g_cfg.hotkey_button_b, g_cfg.hotkey_hold_ms,
      g_cfg.theme);
  if (n <= 0 || (size_t)n >= sizeof(txt)) {
    return -1;
  }
  return write_file_atomic(CHEATRUNNER_CONFIG_PATH, (const uint8_t *)txt, (size_t)n);
}

int
config_load(void) {
  pthread_mutex_lock(&g_cfg_lock);
  config_set_defaults(&g_cfg);
  cr_log_set_level(g_cfg.log_level);
  FILE *fp = fopen(CHEATRUNNER_CONFIG_PATH, "rb");
  if (!fp) {
    int rc = config_save_locked();
    pthread_mutex_unlock(&g_cfg_lock);
    cr_log("info", "config", "created default config (%d)", rc);
    return 0;
  }
  char line[256];
  int valid_lines = 0;
  while (fgets(line, sizeof(line), fp) != NULL) {
    char *eq = strchr(line, '=');
    if (!eq) {
      continue;
    }
    *eq = '\0';
    char *k = line;
    char *v = eq + 1;
    str_trim(k);
    str_trim(v);
    valid_lines++;
    if (!strcmp(k, "http_port")) {
      g_cfg.http_port = atoi(v);
    } else if (!strcmp(k, "rpc_port") || !strcmp(k, "rpc_legacy_enabled") ||
               !strcmp(k, "rpc_json_enabled") || !strcmp(k, "rpc_legacy_port") ||
               !strcmp(k, "rpc_json_port") || !strcmp(k, "rpc_heartbeat_sec") ||
               !strcmp(k, "rpc_emit_cheat_events") ||
               !strcmp(k, "cheat_validate_original_bytes") || !strcmp(k, "cheat_address_auto_detect") ||
               !strcmp(k, "cheat_mc4_address_mode") || !strcmp(k, "cheat_shn_address_mode") ||
               !strcmp(k, "allow_unsafe_mc4_apply") || !strcmp(k, "allow_unsafe_shn_apply") ||
               !strcmp(k, "allow_legacy_mc4_without_expected") || !strcmp(k, "allow_legacy_shn_without_expected") ||
               !strcmp(k, "cheat_mc4_unverified_fallback") || !strcmp(k, "cheat_shn_unverified_fallback") ||
               !strcmp(k, "cheat_log_candidates") || !strcmp(k, "cheat_addr_cache_enabled") ||
               !strcmp(k, "share_hotkey_enabled") || !strcmp(k, "share_hotkey_hold_ms")) {
      /* Ignore removed settings so existing config files still load. */
    } else if (!strcmp(k, "auto_load_cheat_menu")) {
      g_cfg.auto_load_cheat_menu = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "auto_download_missing_cheat")) {
      g_cfg.auto_download_missing_cheat = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "launch_kill_current")) {
      g_cfg.launch_kill_current = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "launch_kill_delay_ms")) {
      g_cfg.launch_kill_delay_ms = atoi(v);
    } else if (!strcmp(k, "launch_wait_timeout_ms")) {
      g_cfg.launch_wait_timeout_ms = atoi(v);
    } else if (!strcmp(k, "cheat_engine")) {
      g_cfg.cheat_engine = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_restore_rx")) {
      g_cfg.cheat_restore_rx = atoi(v) ? 1 : 0;  /* Old key, still accepted; off by default. */
    } else if (!strcmp(k, "cheat_restore_original_prot")) {
      g_cfg.cheat_restore_original_prot = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "allow_force_enable")) {
      g_cfg.allow_force_enable = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_state_after_launch_delay_ms")) {
      g_cfg.cheat_state_after_launch_delay_ms = atoi(v);
    } else if (!strcmp(k, "launch_post_timeout_grace_ms")) {
      g_cfg.launch_post_timeout_grace_ms = atoi(v);
    } else if (!strcmp(k, "launch_user_id")) {
      snprintf(g_cfg.launch_user_id, sizeof(g_cfg.launch_user_id), "%s", v);
    } else if (!strcmp(k, "cheat_post_apply_watch_ms")) {
      g_cfg.cheat_post_apply_watch_ms = atoi(v);
      if (g_cfg.cheat_post_apply_watch_ms < 0) g_cfg.cheat_post_apply_watch_ms = 8000;
    } else if (!strcmp(k, "cheat_mark_crash_suspect")) {
      g_cfg.cheat_mark_crash_suspect = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_apply_one_at_a_time")) {
      g_cfg.cheat_apply_one_at_a_time = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_apply_cooldown_ms")) {
      int cv = atoi(v);
      g_cfg.cheat_apply_cooldown_ms = (cv >= 0 && cv <= 30000) ? cv : 0;
    } else if (!strcmp(k, "cheat_post_apply_poll_ms")) {
      int cv = atoi(v);
      g_cfg.cheat_post_apply_poll_ms = (cv >= 100 && cv <= 5000) ? cv : 500;
    } else if (!strcmp(k, "cheat_min_stable_ms")) {
      int cv = atoi(v);
      g_cfg.cheat_min_stable_ms = (cv >= 0 && cv <= 60000) ? cv : 0;
    } else if (!strcmp(k, "cheat_sources_enabled")) {
      g_cfg.cheat_sources_enabled = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_remote_download_enabled")) {
      g_cfg.cheat_remote_download_enabled = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_source_cache_ttl_seconds") || !strcmp(k, "cheat_source_cache_ttl_sec")) {
      g_cfg.cheat_source_cache_ttl_seconds = atoi(v);
    } else if (!strcmp(k, "cheat_remote_max_file_bytes")) {
      g_cfg.cheat_remote_max_file_bytes = atoi(v);
    } else if (!strcmp(k, "title_lookup_enabled")) {
      g_cfg.title_lookup_enabled = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "title_lookup_cache_enabled")) {
      g_cfg.title_lookup_cache_enabled = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "title_lookup_timeout_ms")) {
      g_cfg.title_lookup_timeout_ms = atoi(v);
    } else if (!strcmp(k, "games_cache_ttl_ms")) {
      g_cfg.games_cache_ttl_ms = atoi(v);
    } else if (!strcmp(k, "appdb_debug_names")) {
      g_cfg.appdb_debug_names = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "log_level")) {
      /* Always starts at debug; a change in Settings lasts until the next start. */
    } else if (!strcmp(k, "cheat_master_code_fixup")) {
      g_cfg.cheat_master_code_fixup = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "cheat_inter_mod_delay_ms")) {
      int cv = atoi(v);
      g_cfg.cheat_inter_mod_delay_ms = (cv >= 0 && cv <= 10000) ? cv : 0;
    } else if (!strcmp(k, "hotkey_enabled")) {
      g_cfg.hotkey_enabled = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "hotkey_button_a")) {
      if (cr_hotkey_button_value(v)) {
        snprintf(g_cfg.hotkey_button_a, sizeof(g_cfg.hotkey_button_a), "%s", v);
      }
    } else if (!strcmp(k, "hotkey_button_b")) {
      if (cr_hotkey_button_value(v)) {
        snprintf(g_cfg.hotkey_button_b, sizeof(g_cfg.hotkey_button_b), "%s", v);
      }
    } else if (!strcmp(k, "hotkey_hold_ms")) {
      int cv = atoi(v);
      g_cfg.hotkey_hold_ms = (cv >= 100 && cv <= 5000) ? cv : 800;
    } else if (!strcmp(k, "klog_enabled") || !strcmp(k, "klog_host") ||
               !strcmp(k, "klog_port") || !strcmp(k, "klog_ring_capacity")) {
      /* Ignore deprecated settings in existing config files. */
    } else if (!strcmp(k, "dev_reload_enabled")) {
      g_cfg.dev_reload_enabled = atoi(v) ? 1 : 0;
    } else if (!strcmp(k, "dev_reload_token")) {
      /* Ignore removed settings so existing config files still load. */
    } else if (!strcmp(k, "dev_shutdown_delay_ms")) {
      g_cfg.dev_shutdown_delay_ms = atoi(v);
    } else if (!strcmp(k, "theme")) {
      snprintf(g_cfg.theme, sizeof(g_cfg.theme), "%s", v);
    } else if (!strcmp(k, "tile_autoinstall_enabled")) {
      g_cfg.tile_autoinstall_enabled = atoi(v) ? 1 : 0;
    }
  }
  fclose(fp);
  {
    struct stat _st;
    if (stat(CHEATRUNNER_CONFIG_PATH, &_st) == 0)
      g_cfg_mtime = _st.st_mtime;
  }
  if (valid_lines == 0) {
    cr_log("warn", "config", "config file exists but contains no valid key=value entries — using defaults");
  }
  if (g_cfg.cheat_state_after_launch_delay_ms < 0 || g_cfg.cheat_state_after_launch_delay_ms > 60000) {
    g_cfg.cheat_state_after_launch_delay_ms = 8000;
    cr_log("warn", "config", "invalid cheat_state_after_launch_delay_ms, using default %d",
           g_cfg.cheat_state_after_launch_delay_ms);
  }
  if (g_cfg.dev_shutdown_delay_ms < 0 || g_cfg.dev_shutdown_delay_ms > 10000) {
    g_cfg.dev_shutdown_delay_ms = 700;
    cr_log("warn", "config", "invalid dev_shutdown_delay_ms, using default %d", g_cfg.dev_shutdown_delay_ms);
  }
  if (g_cfg.cheat_source_cache_ttl_seconds < 60 || g_cfg.cheat_source_cache_ttl_seconds > 604800) {
    g_cfg.cheat_source_cache_ttl_seconds = 21600;
  }
  if (g_cfg.cheat_remote_max_file_bytes < 1024 || g_cfg.cheat_remote_max_file_bytes > (8 * 1024 * 1024)) {
    g_cfg.cheat_remote_max_file_bytes = 1048576;
  }
  if (g_cfg.title_lookup_timeout_ms < 1000 || g_cfg.title_lookup_timeout_ms > 30000) {
    g_cfg.title_lookup_timeout_ms = 8000;
  }
  if (g_cfg.games_cache_ttl_ms < 1000 || g_cfg.games_cache_ttl_ms > 300000) {
    g_cfg.games_cache_ttl_ms = 30000;
  }
  g_cfg.appdb_debug_names = g_cfg.appdb_debug_names ? 1 : 0;
  if (!strcmp(g_cfg.hotkey_button_a, g_cfg.hotkey_button_b)) {
    cr_log("warn", "config", "hotkey_button_a and hotkey_button_b are the same, resetting to defaults");
    snprintf(g_cfg.hotkey_button_a, sizeof(g_cfg.hotkey_button_a), "%s", "L2");
    snprintf(g_cfg.hotkey_button_b, sizeof(g_cfg.hotkey_button_b), "%s", "R3");
  }
  if (strcasecmp(g_cfg.log_level, "debug") != 0 &&
      strcasecmp(g_cfg.log_level, "info") != 0 &&
      strcasecmp(g_cfg.log_level, "warn") != 0 &&
      strcasecmp(g_cfg.log_level, "error") != 0) {
    snprintf(g_cfg.log_level, sizeof(g_cfg.log_level), "%s", "debug");
  }
  cr_log_set_level(g_cfg.log_level);
  pthread_mutex_unlock(&g_cfg_lock);
  cr_log("info", "config", "config loaded");
  return 0;
}

void
config_check_reload(void) {
  struct stat st;
  if (stat(CHEATRUNNER_CONFIG_PATH, &st) != 0) return;
  pthread_mutex_lock(&g_cfg_lock);
  time_t last_mtime = g_cfg_mtime;
  pthread_mutex_unlock(&g_cfg_lock);
  if (st.st_mtime == last_mtime) return;
  cr_log("info", "config", "config file changed on disk, reloading");
  config_load();
}
