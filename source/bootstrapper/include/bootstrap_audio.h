/* Copyright (C) 2026 HyperHEN contributors */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Play the embedded HyperHEN startup voice. Failure never aborts startup. */
bool bootstrap_audio_play_hype_voice(void);

#ifdef __cplusplus
}
#endif
