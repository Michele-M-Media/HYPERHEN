/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Launch the owned util, kstuff and daemon dependency chain. */
int bootstrap_launch_services(uint32_t firmware_version, bool kstuff_autoload);

/**
 * Consume the clean-boot Linux marker and launch the embedded HyperLinux ELF
 * before HyperCore. Returns 0 when not armed, 1 after a successful handoff,
 * and -1 when an armed request cannot be completed.
 */
int bootstrap_linux_boot_if_armed(uint32_t firmware_version);

#ifdef __cplusplus
}
#endif
