#ifndef SM_SHELLCORE_HOOKS_H
#define SM_SHELLCORE_HOOKS_H

#include <stdbool.h>

// Install/remove the fail-open SceShellCore lifecycle bridge.
bool sm_shellcore_hooks_start(void);
void sm_shellcore_hooks_stop(void);

// Return true only while the verified lifecycle bridge is installed and
// ready. Offset-free lifecycle fallbacks use this to avoid racing the
// firmware-specific pre-launch path.
bool sm_shellcore_hooks_active(void);

// Check/repair previously installed hooks; disabled hooks remain disabled.
bool sm_shellcore_hooks_refresh(void);

// Return whether this firmware uses the internal TitleDir bridge.
bool sm_shellcore_install_bridge_enabled(void);

// Invoke the internal TitleDir path through one armed AppInstallAll dispatch.
// Returns false when the bridge is unavailable or was not consumed.
bool sm_shellcore_install_title_dir(const char *title_id,
                                    const char *install_dir,
                                    int *result_out);

#endif
