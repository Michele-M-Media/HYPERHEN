/* SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted activation sequence: TacHen platform/game/game-gate.c.
 * See third_party/tachen-plugin-runtime/NOTICE.md.
 */
#include <onion/plugin_worker.h>
typedef int (*Load)(const char *, uint64_t, const void *, unsigned,
                    const void *, int *);
typedef int (*Dlsym)(int, const char *, void **);
typedef int (*Activate)(int, const char **);

__attribute__((section(".text.entry"), noinline))
void *hyperhen_plugin_worker(void *argument) {
  HyperhenPluginWorker *ctx = argument;
  int initialized = 0;
  ctx->handle = ((Load)(uintptr_t)ctx->load_start_module)(
      ctx->path, 0, 0, 0, 0, &initialized);
  ctx->start_result = initialized;
  unsigned state = HH_PLUGIN_FAILED;
  if (ctx->handle >= 0 && initialized >= 0) {
    void *activate = 0, *unload = 0;
    Dlsym resolve = (Dlsym)(uintptr_t)ctx->dlsym;
    /* Paired exports identify the extra activation contract. A plain
     * module_start-only PRX is NOT initialized twice. */
    if (resolve && !resolve(ctx->handle, "plugin_load", &activate) && activate &&
        !resolve(ctx->handle, "plugin_unload", &unload) && unload) {
      ctx->activated = 1;
      ctx->activation_result = ((Activate)activate)(0, 0);
      if (!ctx->activation_result) state = HH_PLUGIN_STARTED;
    } else state = HH_PLUGIN_STARTED;
  }
  __atomic_store_n(&ctx->state, state, __ATOMIC_RELEASE);
  return 0;
}
