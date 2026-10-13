/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BootstrapConfig {
  uint32_t firmware_version;
  bool kstuff_autoload;
  bool hype_voice;
} BootstrapConfig;

/** Configure notifications/logging and resolve the startup settings. */
bool bootstrap_config_load(BootstrapConfig *config);

#ifdef __cplusplus
}
#endif
