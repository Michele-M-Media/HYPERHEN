/* Copyright (C) 2025 etaHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.

Jump uses mov r11,imm64/jmp r11 (FF25 faults on XO pages). Pages become writable
through mprotect, with kernel_mprotect as the fallback. */

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/param.h>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>
#include <ps5/mdbg.h>

#include "cr_detour.h"
#include "cr_plog.h"
#include "cr_reloc.h"

static void
cr_write_jump(uint64_t address, uint64_t destination) {
  *(uint8_t *)(address)     = 0xFF;
  *(uint8_t *)(address + 1) = 0x25;
  *(uint8_t *)(address + 2) = 0x00;
  *(uint8_t *)(address + 3) = 0x00;
  *(uint8_t *)(address + 4) = 0x00;
  *(uint8_t *)(address + 5) = 0x00;
  *(uint64_t *)(address + 6) = destination;
}

/* mov r11,imm64/jmp r11/nop — same 14 bytes as cr_write_jump but reads
 * nothing off the page, so it works on execute-only pages. Clobbers r11. */
static void
cr_build_xo_safe_jump(uint8_t out[CR_HOOK_LENGTH], uint64_t destination) {
  out[0] = 0x49; out[1] = 0xBB;                     /* mov r11, imm64 */
  memcpy(out + 2, &destination, 8);
  out[10] = 0x41; out[11] = 0xFF; out[12] = 0xE3;   /* jmp r11 */
  out[13] = 0x90;                                   /* nop pad */
}

void
cr_patch_in_jump(uint64_t address, void *destination) {
  if (!address || !destination) {
    return;
  }
  cr_write_jump(address, (uint64_t)destination);
}

static void
log_bytes(const char *what, const uint8_t *p, size_t n) {
  char hex[3 * 32 + 1];
  size_t k = n > 32 ? 32 : n;
  for (size_t i = 0; i < k; i++) {
    snprintf(hex + 3 * i, 4, "%02x ", p[i]);
  }
  hex[3 * k] = 0;
  cr_plog("detour: %s %s\n", what, hex);
}

/* A userland mprotect is enough on firmwares that allow it. */
static int
mprotect_rwx(uint64_t addr, size_t len) {
  if (mprotect((void *)addr, len, PROT_EXEC | PROT_READ | PROT_WRITE) == 0) {
    cr_plog("detour: mprotect 0x%llx len=0x%zx ok (userland)\n", (unsigned long long)addr, len);
    return 0;
  }
  int err = errno;
  int rc = kernel_mprotect(getpid(), addr, len, PROT_EXEC | PROT_READ | PROT_WRITE);
  cr_plog("detour: mprotect 0x%llx len=0x%zx userland errno=%d, kernel rc=%d\n",
              (unsigned long long)addr, len, err, rc);
  return rc;
}

/* Maps a page within rel32 reach of target so rip-relative operands can be relocated. */
static uint8_t *
alloc_near(uint64_t target, size_t len) {
  for (int step = 0; step <= 31; step++) {
    for (int sign = 0; sign < 2; sign++) {
      uint64_t dist = (uint64_t)step * 0x4000000ull;
      uint64_t hint = sign ? target - dist : target + dist;
      if ((step == 0 && sign) || hint == 0) {
        continue;
      }
      void *p = mmap((void *)(hint & ~(uint64_t)(PAGE_SIZE - 1)), len, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED) {
        continue;
      }
      int64_t d = (int64_t)((uint64_t)p - target);
      if (d > -0x7fff0000ll && d < 0x7fff0000ll) {
        cr_plog("detour: trampoline page %p, %lld bytes from the target\n", p, (long long)d);
        return p;
      }
      munmap(p, len);
    }
  }
  return NULL;
}

void *
cr_detour_prepare(uint64_t address, void *destination, cr_hook_site_t *site) {
  if (!address || !destination || !site) {
    return NULL;
  }

  pid_t pid = getpid();
  cr_plog("detour: preparing 0x%llx -> %p\n", (unsigned long long)address, destination);

  /* read the prologue via mdbg_copyout, not a raw pointer deref */
  uint8_t prologue[32];
  memset(prologue, 0x00, sizeof(prologue));
  if (mdbg_copyout(pid, (intptr_t)address, prologue, sizeof(prologue)) != 0) {
    cr_plog("cr_detour_prepare: mdbg_copyout of target prologue failed\n");
    return NULL;
  }

  log_bytes("prologue", prologue, 28);

  /* mov r11,imm64 is our own jump - already there means an earlier injection hooked it. */
  if (prologue[0] == 0x49 && prologue[1] == 0xBB && prologue[10] == 0x41 && prologue[11] == 0xFF &&
      prologue[12] == 0xE3) {
    cr_plog("cr_detour_prepare: target already hooked, skipping\n");
    return CR_DETOUR_ALREADY_HOOKED;
  }

  uint8_t *executable_address = alloc_near(address, PAGE_SIZE);
  if (!executable_address) {
    cr_plog("cr_detour_prepare: failed to allocate memory for stub\n");
    return NULL;
  }

  size_t stolen = 0;
  const char *why = "unknown";
  if (!cr_relocate(prologue, address, executable_address, (uint64_t)executable_address,
                   CR_HOOK_LENGTH, CR_RELOC_CAPACITY, &stolen, &why)) {
    cr_plog("cr_detour_prepare: can't relocate the prologue at 0x%llx: %s\n",
                (unsigned long long)address, why);
    munmap(executable_address, PAGE_SIZE);
    return NULL;
  }

  cr_plog("detour: relocated %zu stolen bytes into the trampoline at %p\n", stolen, executable_address);
  log_bytes("trampoline", executable_address, 32);

  if (mprotect_rwx((uint64_t)executable_address, PAGE_SIZE) != 0) {
    cr_plog("cr_detour_prepare: mprotect on trampoline failed\n");
    munmap(executable_address, PAGE_SIZE);
    return NULL;
  }

  /* The jump and the decoded prologue can run past the first page. */
  uint64_t page = address & ~(uint64_t)(PAGE_SIZE - 1);
  uint64_t end = (address + CR_HOOK_LENGTH + 14 + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
  if (mprotect_rwx(page, (size_t)(end - page)) != 0) {
    cr_plog("cr_detour_prepare: mprotect on target page failed, bailing out\n");
    munmap(executable_address, PAGE_SIZE);
    return NULL;
  }

  /* Hand the bytes to the daemon instead of storing them here - it writes them
   * with the whole process suspended, so nothing can be executing them. */
  site->target = address;
  site->len = CR_HOOK_LENGTH;
  site->done = 0;
  memcpy(site->expect, prologue, CR_HOOK_MAX_BYTES);
  cr_build_xo_safe_jump(site->bytes, (uint64_t)destination);
  log_bytes("jump", site->bytes, CR_HOOK_LENGTH);
  cr_plog("detour: 0x%llx ready, trampoline %p\n", (unsigned long long)address, executable_address);

  return executable_address;
}

/* Swaps in the 16 bytes only if they are still the expected ones, atomically. */
static int
cr_cas16(uint64_t address, const uint8_t expect[16], const uint8_t desired[16]) {
  struct block { uint64_t lo, hi; } __attribute__((aligned(16)));
  struct block e, d;
  memcpy(&e, expect, 16);
  memcpy(&d, desired, 16);

  unsigned char swapped;
  __asm__ volatile("lock; cmpxchg16b %1; sete %0"
                   : "=q"(swapped), "+m"(*(volatile struct block *)address), "+a"(e.lo), "+d"(e.hi)
                   : "b"(d.lo), "c"(d.hi)
                   : "cc", "memory");
  return swapped;
}

int
cr_detour_commit(cr_hook_site_t *site) {
  uint8_t block[CR_HOOK_MAX_BYTES];

  if (!site || site->done || site->len != CR_HOOK_LENGTH || (site->target & 15) != 0) {
    cr_plog("detour: 0x%llx can't be armed in place (not 16-byte aligned)\n",
            site ? (unsigned long long)site->target : 0ull);
    return -1;
  }
  memcpy(block, site->bytes, site->len);
  memcpy(block + site->len, site->expect + site->len, CR_HOOK_MAX_BYTES - site->len);

  if (!cr_cas16(site->target, site->expect, block)) {
    cr_plog("detour: 0x%llx changed since it was prepared, leaving it to the daemon\n",
            (unsigned long long)site->target);
    return -1;
  }

  site->done = 1;
  cr_plog("detour: 0x%llx armed in place\n", (unsigned long long)site->target);
  return 0;
}
