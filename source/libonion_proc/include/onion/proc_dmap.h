/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Cross-process user VA copy via kernel DMAP (virt2phys + kernel_copyout).
 * Shared by cheats (KdirectBackend) and FPS skip-hook sampling.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct onion_proc_dmap_ctx {
  pid_t pid;
  uint64_t cr3;
  uint64_t dmap;
};

#ifdef __cplusplus
extern "C" {
#endif

/** Copy `n` bytes from `va` in `pid` into `dst`. 0 on success, -1 on failure. */
int onion_proc_copyout(pid_t pid, uint64_t va, void *dst, size_t n);

/** Copy `n` bytes from `src` into `va` in `pid`. 0 on success, -1 on failure. */
int onion_proc_copyin(pid_t pid, uint64_t va, const void *src, size_t n);

/** Resolve a process page-table context for repeated reads. */
int onion_proc_dmap_init(pid_t pid, struct onion_proc_dmap_ctx *ctx);

/** Translate one user VA using a previously initialized context. */
int onion_proc_translate(const struct onion_proc_dmap_ctx *ctx, uint64_t va,
                         uint64_t *phys, uint64_t *phys_limit);

/** Read a physical range through the context's DMAP window. */
int onion_proc_copyout_phys(const struct onion_proc_dmap_ctx *ctx,
                            uint64_t phys, void *dst, size_t n);

#ifdef __cplusplus
}
#endif
