#ifndef SM_SHELLCORE_SERVICE_H
#define SM_SHELLCORE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "sm_limits.h"

// Start/stop the local Unix-socket owner used by the SceShellCore bridge.
bool sm_shellcore_service_start(void);
void sm_shellcore_service_stop(void);
// Close the bridge socket on sleep entry and recreate it after resume.
void sm_shellcore_service_on_sleep_change(bool active);

// Release transient title/image mounts after launch failure or explicit API use.
bool sm_shellcore_release_title_runtime(const char *title_id);
// Public API entry points. Return 0 or a positive errno value. A NULL mount
// mode uses the configured per-image/global mode.
int sm_shellcore_mount_title_runtime(const char *title_id,
                                     const bool *mount_read_only_override);
int sm_shellcore_unmount_title_runtime(const char *title_id);
// Bind an incoming/outgoing managed title to its current app id and PID.
// Replacing an outgoing PID preserves the incoming launch request.
// Returns false while release is in progress or a managed owner is missing.
bool sm_shellcore_service_bind_prepared_app(const char *title_id,
                                            uint32_t app_id, pid_t pid);
// Publish process exit while ShellCore still owns the title sandbox and copy
// the matching owned title to title_id_out. exit_time_us is the original
// monotonic process exit timestamp; zero uses the current time.
// Returns true when an owned incoming/outgoing title became exit-pending.
bool sm_shellcore_service_note_game_exit(pid_t pid, uint64_t exit_time_us,
                                         char title_id_out[MAX_TITLE_ID]);
// Release exit-pending runtime stacks whose /mnt/sandbox/<TITLE_ID>_* entries
// have disappeared or outlived the bounded exit grace. Returns 0 when no
// exit-pending runtime remains, EAGAIN
// while a sandbox remains, or another positive errno after a release failure.
int sm_shellcore_service_release_exited_titles(void);
// Next exit cleanup check, or zero when neither runtime owner is exit-pending.
uint64_t sm_shellcore_service_next_exit_cleanup_us(uint64_t now_us);
// Return true while ShellCore still exposes a sandbox for the title.
bool sm_shellcore_service_title_sandbox_exists(const char *title_id);
// Ensure that a managed title has its image/nullfs/backport runtime stack.
// Unmanaged stock titles are treated as already ready.
bool sm_shellcore_ensure_title_runtime(const char *title_id);
// Return true when the named title owns an incoming or outgoing runtime mount.
bool sm_shellcore_service_title_is_prepared(const char *title_id);
// Return true while ShellCore owns any title mount prepared for launch or exit.
bool sm_shellcore_service_has_prepared_mount(void);
// Background work may run alongside homebrew or a confirmed unfocused game.
// Launch preparation, foreground games and pending exits remain blocking.
bool sm_shellcore_background_work_blocked(void);
// Protect live runtime owners, including a stock PKG without managed links.
bool sm_shellcore_title_is_protected(const char *title_id);
// Includes source ancestors/children and every layer of an owned image chain.
bool sm_shellcore_source_is_protected(const char *path);
// Exclude new launch preparation while an external AppInst mutation runs.
bool sm_shellcore_try_begin_external_mutation(void);
void sm_shellcore_end_external_mutation(void);

#endif
