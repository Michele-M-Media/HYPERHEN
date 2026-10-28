/* SPDX-License-Identifier: GPL-3.0-or-later
 * HyperHEN game-owned worker; activation contract follows TacHen game-gate.c.
 * No hooks, graphics, filesystem access or privilege changes on this thread.
 */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { HH_PLUGIN_PENDING=0, HH_PLUGIN_STARTED=1, HH_PLUGIN_FAILED=2 };
typedef struct {
  uint64_t load_start_module, dlsym;
  int32_t handle, start_result, activation_result;
  uint32_t activated, state;
  char path[512];
} HyperhenPluginWorker;
void *hyperhen_plugin_worker(void *argument);
#ifdef __cplusplus
}
#endif
