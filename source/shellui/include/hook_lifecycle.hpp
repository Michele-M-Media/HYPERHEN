/* Copyright (C) 2026 OnionHEN */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hook callbacks remain pass-through until the complete install transaction commits. */
void shellui_hooks_begin_install(void);
void shellui_hooks_publish_ready(void);
void shellui_hooks_publish_failed(void);
bool shellui_hooks_are_ready(void);

#ifdef __cplusplus
}
#endif
