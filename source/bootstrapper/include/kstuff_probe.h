/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Detect whether the kstuff capability is already active.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * True when userland mprotect succeeds with the kernel patches active. Process
 * names are not ownership evidence because user Payload names are unrestricted.
 */
bool kstuff_already_running(void);

#ifdef __cplusplus
}
#endif
