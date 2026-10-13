/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Materialize embedded assets and report whether the startup icon is ready. */
bool bootstrap_assets_write(void);

#ifdef __cplusplus
}
#endif
