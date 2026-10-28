/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <onion/boot_mode_5xx.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BootstrapLaunchResult {
  BOOTSTRAP_LAUNCH_FULL = 0,
  BOOTSTRAP_LAUNCH_USERLAND_ONLY = 1,
  BOOTSTRAP_LAUNCH_CORE_ONLY = 2,
} BootstrapLaunchResult;

/**
 * Launch the firmware-appropriate owned service chain. Negative values are
 * failures; non-negative BootstrapLaunchResult values describe the active
 * capability level.
 */
int bootstrap_launch_services(uint32_t firmware_version, bool kstuff_autoload,
                              Onion5xxBootMode boot_mode_5xx);

/**
 * Consume the clean-boot Linux marker and launch the embedded HyperLinux ELF
 * before HyperCore. Returns 0 when not armed, 1 after a successful handoff,
 * and -1 when an armed request cannot be completed.
 */
int bootstrap_linux_boot_if_armed(uint32_t firmware_version);

#ifdef __cplusplus
}
#endif
