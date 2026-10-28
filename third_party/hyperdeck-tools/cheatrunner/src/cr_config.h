#ifndef CR_CONFIG_H
#define CR_CONFIG_H

#include <pthread.h>
#include <stddef.h>
#include "cr_paths.h"

#define CHEATRUNNER_HTTP_PORT 9999

typedef struct cheatrunner_config {
  int http_port;
  int auto_load_cheat_menu;
  int auto_download_missing_cheat;
  int launch_kill_current;
  int launch_kill_delay_ms;
  int launch_wait_timeout_ms;
  int cheat_engine;
  int cheat_restore_rx;          /* Deprecated, use cheat_restore_original_prot. */
  int cheat_restore_original_prot;
  int allow_force_enable;
  int cheat_state_after_launch_delay_ms;
  int launch_post_timeout_grace_ms;
  int dev_reload_enabled;
  int dev_shutdown_delay_ms;
  char theme[16];
  char launch_user_id[32];
  int cheat_post_apply_watch_ms;
  int cheat_mark_crash_suspect;
  int cheat_apply_one_at_a_time;
  int cheat_apply_cooldown_ms;
  int cheat_post_apply_poll_ms;
  int cheat_min_stable_ms;
  int cheat_sources_enabled;
  int cheat_remote_download_enabled;
  int cheat_source_cache_ttl_seconds;
  int cheat_remote_max_file_bytes;
  int title_lookup_enabled;
  int title_lookup_cache_enabled;
  int title_lookup_timeout_ms;
  int games_cache_ttl_ms;
  int appdb_debug_names;
  char log_level[16];
  int cheat_master_code_fixup;
  int cheat_inter_mod_delay_ms;
  int tile_autoinstall_enabled;
  int  hotkey_enabled; /* master switch, off by default - disables all SceShellUI hooking */
  char hotkey_button_a[16]; /* see CR_HOTKEY_BUTTONS in cr_hotkey_buttons.h */
  char hotkey_button_b[16];
  int  hotkey_hold_ms;
} cheatrunner_config_t;

extern pthread_mutex_t      g_cfg_lock;
extern cheatrunner_config_t g_cfg;

void config_set_defaults(cheatrunner_config_t *cfg);
int  config_load(void);
int  config_save_locked(void);
void config_check_reload(void);


#endif /* CR_CONFIG_H */
