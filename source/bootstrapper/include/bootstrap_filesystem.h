/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void bootstrap_filesystem_create_directories(void);
bool bootstrap_filesystem_mount_system(void);
void bootstrap_filesystem_disable_updates(void);

#ifdef __cplusplus
}
#endif
