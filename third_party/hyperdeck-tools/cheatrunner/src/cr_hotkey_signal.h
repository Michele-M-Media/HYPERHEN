#ifndef CR_HOTKEY_SIGNAL_H
#define CR_HOTKEY_SIGNAL_H

#include <stdint.h>

/* Loopback port for signals from the injected payload (not the HTTP port): "ok"/"old" (hook install
 * result), "hotkey" (combo held), "menu:<titleid>" (OptionsMenu row clicked). */
#define CR_HOTKEY_SIGNAL_PORT 39217

/* Prefix of the OptionsMenu click message: "menu:" + up to 15 title id chars (running_game_state_t.title_id). */
#define CR_MENU_SIGNAL_PREFIX "menu:"
#define CR_MENU_SIGNAL_PREFIX_LEN 5

/* Daemon to payload config broadcast (combo, hold_ms), on a separate port from the hotkey signal above. */
#define CR_HOTKEY_CONFIG_SIGNAL_PORT 39218

/* Payload log lines forwarded to the daemon: "log:" + text. */
#define CR_LOG_SIGNAL_PREFIX "log:"
#define CR_LOG_SIGNAL_PREFIX_LEN 4
#define CR_LOG_SIGNAL_MAX 400

/* PID of the SceShellUI instance that already has its hooks armed. */
#define CR_HOTKEY_READY_FILE "/system_tmp/cheatrunner/hotkey.pid"

typedef struct {
  uint32_t combo;
  uint32_t hold_ms;
} cr_hotkey_config_packet_t;

/* Hook sites the payload prepared. It arms the 16-byte aligned ones itself with one atomic store
 * (done=1); the daemon writes the rest with SceShellUI suspended. */
#define CR_HOOK_REQUEST_MAGIC 0x4B4F4F48u /* "HOOK" */
#define CR_HOOK_MAX_SITES     4
#define CR_HOOK_MAX_BYTES     16

#define CR_HOOK_KIND_GETDATA 0
#define CR_HOOK_KIND_MENU    1
#define CR_HOOK_KIND_BOOT    2

typedef struct {
  uint64_t target;
  uint32_t len;
  uint8_t  bytes[CR_HOOK_MAX_BYTES];
  uint8_t  expect[CR_HOOK_MAX_BYTES];
  uint8_t  kind;
  uint8_t  done;
} cr_hook_site_t;

typedef struct {
  uint32_t       magic;
  uint32_t       count;
  cr_hook_site_t sites[CR_HOOK_MAX_SITES];
} cr_hook_request_t;

#endif /* CR_HOTKEY_SIGNAL_H */
