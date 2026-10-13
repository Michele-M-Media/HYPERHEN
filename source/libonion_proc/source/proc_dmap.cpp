/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Read/write another process's user VA through the kernel DMAP window.
 * Algorithm matches util KdirectBackend (cheats/memory_backends.cpp) and
 * kstuff-lite virt2phys: CR3 from vmspace.pmap, then 4-level PT walk.
 */
#include <onion/proc_dmap.h>

#include <ps5/kernel.h>

#include <cstdint>

namespace {

constexpr uint64_t kPtePresent = 1;
constexpr uint64_t kPteLarge = 128;
constexpr uint64_t kPhysMask52 = (1ULL << 52) - 1ULL;

int pmap_offset(uint32_t fw_raw) {
  /* kernel_get_fw_version is 0xAABB0000; util_system_fw_major is >> 16. */
  const uint32_t major = (fw_raw > 0xFFFFu) ? (fw_raw >> 16) : fw_raw;
  if (major >= 0x600u)
    return 0x2E8;
  if (major >= 0x300u)
    return 0x2E0;
  return 0x2C0; /* 1.xx — kstuff-lite shellui_patch.c */
}

int get_proc_cr3(pid_t pid, uint64_t *cr3, uint64_t *dmap_base) {
  const intptr_t proc = kernel_get_proc(pid);
  if (proc == 0)
    return -1;

  uint64_t vmspace = 0;
  if (kernel_copyout(proc + KERNEL_OFFSET_PROC_P_VMSPACE, &vmspace,
                     sizeof(vmspace)) != 0 ||
      vmspace == 0)
    return -1;

  uint64_t ptrs[2] = {0, 0};
  if (kernel_copyout(static_cast<intptr_t>(vmspace + pmap_offset(kernel_get_fw_version()) +
                                           32),
                     ptrs, sizeof(ptrs)) != 0)
    return -1;

  if (ptrs[1] == 0)
    return -1;
  if (cr3)
    *cr3 = ptrs[1];
  if (dmap_base)
    *dmap_base = ptrs[0] - ptrs[1];
  return 0;
}

uint64_t virt2phys(uintptr_t addr, uint64_t dmap, uint64_t pml,
                   uint64_t *phys_limit) {
  for (int i = 39; i >= 12; i -= 9) {
    uint64_t inner = 0;
    const uint64_t pte =
        dmap + pml + ((addr & (0x1ffULL << i)) >> (i - 3));
    if (kernel_copyout(static_cast<intptr_t>(pte), &inner, sizeof(inner)) != 0)
      return static_cast<uint64_t>(-1);
    if ((inner & kPtePresent) == 0)
      return static_cast<uint64_t>(-1);
    if ((inner & kPteLarge) || i == 12) {
      inner &= kPhysMask52 - ((1ULL << i) - 1ULL);
      inner |= addr & ((1ULL << i) - 1ULL);
      if (phys_limit)
        *phys_limit = (inner | ((1ULL << i) - 1ULL)) + 1ULL;
      return inner;
    }
    inner &= kPhysMask52 - ((1ULL << 12) - 1ULL);
    pml = inner;
  }
  return static_cast<uint64_t>(-1);
}

int copy_via_dmap(pid_t pid, uint64_t addr, void *buf, size_t len, bool to_proc) {
  uint64_t cr3 = 0;
  uint64_t dmap = 0;
  if (get_proc_cr3(pid, &cr3, &dmap) < 0)
    return -1;

  auto *p = static_cast<uint8_t *>(buf);
  uint64_t vaddr = addr;
  size_t left = len;
  while (left > 0) {
    uint64_t phys_end = 0;
    const uint64_t phys = virt2phys(static_cast<uintptr_t>(vaddr), dmap, cr3,
                                    &phys_end);
    if (phys == static_cast<uint64_t>(-1))
      return -1;
    size_t chunk = static_cast<size_t>(phys_end - phys);
    if (left < chunk)
      chunk = left;
    const int rc =
        to_proc ? kernel_copyin(p, static_cast<intptr_t>(dmap + phys), chunk)
                : kernel_copyout(static_cast<intptr_t>(dmap + phys), p, chunk);
    if (rc != 0)
      return -1;
    vaddr += chunk;
    p += chunk;
    left -= chunk;
  }
  return 0;
}

} // namespace

extern "C" int onion_proc_copyout(pid_t pid, uint64_t va, void *dst, size_t n) {
  if (pid <= 0 || dst == nullptr || n == 0)
    return -1;
  return copy_via_dmap(pid, va, dst, n, false);
}

extern "C" int onion_proc_copyin(pid_t pid, uint64_t va, const void *src,
                                 size_t n) {
  if (pid <= 0 || src == nullptr || n == 0)
    return -1;
  return copy_via_dmap(pid, va, const_cast<void *>(src), n, true);
}

extern "C" int onion_proc_dmap_init(pid_t pid, onion_proc_dmap_ctx *ctx) {
  if (!ctx || pid <= 0)
    return -1;
  ctx->pid = 0;
  ctx->cr3 = 0;
  ctx->dmap = 0;
  if (get_proc_cr3(pid, &ctx->cr3, &ctx->dmap) < 0)
    return -1;
  ctx->pid = pid;
  return 0;
}

extern "C" int onion_proc_translate(const onion_proc_dmap_ctx *ctx,
                                     uint64_t va, uint64_t *phys,
                                     uint64_t *phys_limit) {
  if (!ctx || !phys || ctx->pid <= 0 || ctx->cr3 == 0 || ctx->dmap == 0)
    return -1;
  const uint64_t value =
      virt2phys(static_cast<uintptr_t>(va), ctx->dmap, ctx->cr3, phys_limit);
  if (value == static_cast<uint64_t>(-1))
    return -1;
  *phys = value;
  return 0;
}

extern "C" int onion_proc_copyout_phys(const onion_proc_dmap_ctx *ctx,
                                        uint64_t phys, void *dst, size_t n) {
  if (!ctx || ctx->pid <= 0 || ctx->dmap == 0 || !dst || n == 0)
    return -1;
  return kernel_copyout(static_cast<intptr_t>(ctx->dmap + phys), dst, n);
}
