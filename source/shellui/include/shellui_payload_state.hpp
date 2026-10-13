/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Runtime-state helpers for toolbox payload entries.
 */
#pragma once

#include <cstddef>
#include <sys/types.h>

void shellui_payload_pid_path(char *out, size_t out_sz, const char *key);
pid_t shellui_payload_read_pid_file(const char *pid_path);
pid_t shellui_payload_validate_pid(pid_t pid, const char *pid_path,
                                   const char *key);
pid_t shellui_payload_resolve_recorded_pid(const char *key, char *pid_path,
                                           size_t pid_path_sz);

inline bool shellui_payload_is_running(const char *key) {
  return shellui_payload_resolve_recorded_pid(key, nullptr, 0) > 1;
}
