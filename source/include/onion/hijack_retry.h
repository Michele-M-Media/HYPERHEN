/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure jailbreak hijack-retry policy (host-testable).
 *
 * After getHijacker(pid) fails:
 *   - stop if process is dead (nothing left to attach)
 *   - stop if attempt count exceeded max
 *   - otherwise retry while the target is still alive
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param process_alive  true if pid still exists
 * @param attempt        1-based attempt number after this failure (first fail = 1)
 * @param max_attempts   stop after this many failed attempts (default policy 30)
 * @return true if the retry loop should abort (fail)
 */
static inline bool onion_hijack_retry_should_stop(bool process_alive,
                                                  int attempt,
                                                  int max_attempts) {
  if (max_attempts < 1) {
    max_attempts = 1;
  }
  if (attempt >= max_attempts) {
    return true;
  }
  /* Target gone — no point retrying. */
  if (!process_alive) {
    return true;
  }
  return false;
}

#ifdef __cplusplus
}
#endif
