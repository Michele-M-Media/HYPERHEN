#ifndef SM_GAME_LIFECYCLE_H
#define SM_GAME_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>
#include "sm_limits.h"

// Start the shared game lifecycle watcher used by runtime game features.
bool start_game_lifecycle_watcher(void);
// Wake the shared watcher so pending lifecycle work is processed immediately.
void wake_game_lifecycle_watcher(void);
// Stop the shared watcher and let lifecycle modules clean up tracked state.
void stop_game_lifecycle_watcher(void);
// Return true when a supported game is currently being tracked as running.
bool sm_game_lifecycle_has_active_game(void);
// Copy the protected live title even when returning false, or an empty string
// when no confirmed live title is available. Homebrew may
// allow work in the foreground; ordinary games require confirmed loss of focus.
// Return false for a foreground/unknown-focus game or a pending exit/handoff.
// A NULL output queries only the policy, without formatting the title ID.
bool sm_game_lifecycle_background_title(char title_id[MAX_TITLE_ID]);
// Publish and wake lifecycle processing for an AppFocus change.
void sm_game_lifecycle_note_app_focus(uint32_t app_id);
// Fail closed for ordinary games when the focus flag cannot be read.
void sm_game_lifecycle_invalidate_app_focus(void);
// Ensure the shared watcher is running for runtime game-state tracking.
bool refresh_game_lifecycle_watcher(void);

#endif
