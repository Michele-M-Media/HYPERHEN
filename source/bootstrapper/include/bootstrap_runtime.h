/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Install process signals and raise bootstrapper privileges. */
bool bootstrap_runtime_prepare(void);

/** Enable the optional TCP stdout/stderr sink when its marker is present. */
void bootstrap_runtime_enable_remote_logging(void);

/** Fault-handler callback for process-owned runtime resources. */
void bootstrap_runtime_cleanup(void);

#ifdef __cplusplus
}
#endif
