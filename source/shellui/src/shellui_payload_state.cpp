/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Runtime-state helpers for toolbox payload entries.
 */
#include "shellui_payload_state.hpp"

#include "external_symbols.hpp"

#include <onion/ipc_client.hpp>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

static pid_t shellui_payload_drop_stale_pid(const char *pid_path,
                                            const char *key) {
  LOG_WARN("Stale payload PID file for %s, removing", key ? key : "?");
  if (pid_path) {
    unlink(pid_path);
  }
  return -1;
}

void shellui_payload_pid_path(char *out, size_t out_sz, const char *key) {
  (void)onion_system_tmp_pid_path(out, out_sz, key);
}

pid_t shellui_payload_read_pid_file(const char *pid_path) {
  if (!pid_path || !pid_path[0]) {
    return -1;
  }
  const int f = open(pid_path, O_RDONLY);
  if (f < 0) {
    return -1;
  }
  char t[32];
  const int r = (int)read(f, t, sizeof(t) - 1);
  close(f);
  if (r <= 0) {
    return -1;
  }
  t[r] = '\0';
  return (pid_t)atoi(t);
}

pid_t shellui_payload_validate_pid(pid_t pid, const char *pid_path,
                                   const char *key) {
  if (pid <= 1) {
    if (pid == 1) {
      LOG_WARN("Ignoring bogus payload PID 1 for %s", key ? key : "?");
      if (pid_path) {
        unlink(pid_path);
      }
    }
    return -1;
  }

  if (!onion_proc_is_alive(pid)) {
    return shellui_payload_drop_stale_pid(pid_path, key);
  }

  char name[32];
  if (sceKernelGetProcessName && sceKernelGetProcessName((int)pid, name) < 0) {
    return shellui_payload_drop_stale_pid(pid_path, key);
  }

  return pid;
}

pid_t shellui_payload_resolve_recorded_pid(const char *key, char *pid_path,
                                           size_t pid_path_sz) {
  char local_pid_path[256];
  char *path = pid_path;
  size_t path_sz = pid_path_sz;

  if (!path || path_sz == 0) {
    path = local_pid_path;
    path_sz = sizeof(local_pid_path);
  }

  shellui_payload_pid_path(path, path_sz, key);
  return shellui_payload_validate_pid(shellui_payload_read_pid_file(path), path,
                                      key);
}
