#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <ps5/kernel.h>

#include "cr_log.h"
#include "cr_config.h"
#include "cr_mdbg.h"
#include "cr_memory.h"
#include "ps5sdk_compat.h"

static int
hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

int
parse_offset_hex_checked(const char *s, uint64_t *out) {
  if (!s || !*s || !out) return -1;
  while (*s == ' ' || *s == '\t') s++;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  if (!*s) return -1;
  for (const char *p = s; *p; p++) {
    if (!isxdigit((unsigned char)*p)) return -1;
  }
  errno = 0;
  unsigned long long v = strtoull(s, NULL, 16);
  if (errno != 0) return -1;
  *out = (uint64_t)v;
  return 0;
}

int
parse_hex_bytes_checked(const char *s, uint8_t *out, size_t out_cap, size_t *out_len) {
  if (!s || !out || !out_len || out_cap == 0) return -1;
  /* Some cheat files drop a leading zero ("1" for "01"); an odd nibble count pairs the first nibble
   * with an implicit zero instead of failing. */
  size_t nibble_count = 0;
  for (const char *p = s; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '-' || *p == ',' || *p == ':') continue;
    if (hex_nibble(*p) < 0) return -1;
    nibble_count++;
  }
  if (nibble_count == 0) return -1;
  size_t w = 0;
  int high = (nibble_count % 2 == 1) ? 0 : -1;
  for (const char *p = s; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '-' || *p == ',' || *p == ':') continue;
    int n = hex_nibble(*p);
    if (high < 0) {
      high = n;
    } else {
      if (w >= out_cap) return -1;
      out[w++] = (uint8_t)((high << 4) | n);
      high = -1;
    }
  }
  if (high >= 0 || w == 0) return -1;
  *out_len = w;
  return 0;
}

int
read_process_memory(pid_t pid, intptr_t addr, uint8_t *out, size_t len) {
  if (!out || len == 0) return -1;
  /* No XOM fallback: kernel_get_vmem_protection/kernel_mprotect on a bad address can hang or panic,
   * so an execute-only page just reads as failed. */
  return mdbg_io_copyout(pid, addr, out, len) == 0 ? 0 : -1;
}


intptr_t
cheat_resolve_addr(pid_t pid, intptr_t base, uint64_t off_u, int abs_flag,
                   const uint8_t *on_b, const uint8_t *expect_b, size_t byte_len) {
  /* Unsigned add avoids signed-overflow UB on a huge/garbage off_u. */
  intptr_t abs_addr = (intptr_t)off_u;
  intptr_t rel_addr = (intptr_t)((uint64_t)base + off_u);
  if (abs_flag) return abs_addr;

  /* Only a file-provided baseline can tell the two apart; relative wins ties. */
  if (expect_b && off_u >= 0x200000ULL && pid > 0 && byte_len > 0 && byte_len <= 128 &&
      ADDR_IN_USER_RANGE(rel_addr) && ADDR_IN_USER_RANGE(abs_addr)) {
    uint8_t probe[128];
    if (read_process_memory(pid, rel_addr, probe, byte_len) == 0 &&
        (memcmp(probe, on_b, byte_len) == 0 || memcmp(probe, expect_b, byte_len) == 0))
      return rel_addr;
    if (read_process_memory(pid, abs_addr, probe, byte_len) == 0 &&
        (memcmp(probe, on_b, byte_len) == 0 || memcmp(probe, expect_b, byte_len) == 0))
      return abs_addr;
  }
  return rel_addr;
}

void
bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  out[0] = '\0';
  if (!bytes || len == 0) return;
  size_t pos = 0;
  for (size_t i = 0; i < len; i++) {
    int n = snprintf(out + pos, out_size - pos, "%02X", bytes[i]);
    if (n <= 0 || (size_t)n >= out_size - pos) break;
    pos += (size_t)n;
  }
}


int
resolve_module_base(pid_t pid, const char *module_name, intptr_t eboot_base, intptr_t *out_base) {
  if (!out_base) return -1;
  if (!module_name || !module_name[0]) {
    *out_base = eboot_base;
    return 0;
  }
  uint32_t handle = 0;
  if (kernel_dynlib_handle(pid, module_name, &handle) != 0) {
    cr_log("warn", "cheats.mem", "resolve_module_base: module '%s' not loaded in pid=%d", module_name, (int)pid);
    return -1;
  }
  intptr_t mbase = kernel_dynlib_mapbase_addr(pid, handle);
  if (mbase <= 0) {
    cr_log("warn", "cheats.mem", "resolve_module_base: mapbase invalid for '%s' handle=%u base=0x%lx",
           module_name, handle, (long)mbase);
    return -1;
  }
  cr_log("info", "cheats.mem", "resolve_module_base: '%s' base=0x%lx", module_name, (long)mbase);
  *out_base = mbase;
  return 0;
}

int
process_is_ps2_emu(pid_t pid) {
  uint32_t handle = 0;
  return kernel_dynlib_handle(pid, "libScePs2EmuMenuDialog.sprx", &handle) == 0;
}

int
write_process_memory_forced(pid_t pid, intptr_t addr, const uint8_t *data, size_t len) {
  if (!ADDR_IN_USER_RANGE(addr)) {
    cr_log("error", "cheats.mem",
           "write_rejected addr=0x%lx not_in_user_range — skipping to prevent kernel hang",
           (long)addr);
    return -7;
  }
  /* Split cross-page writes; a precaution left from the kernel_mprotect days. */
  intptr_t next_page = (intptr_t)ROUND_PG_UP((uintptr_t)addr + 1);
  if (addr + (intptr_t)len > next_page) {
    size_t first = (size_t)(next_page - addr);
    int r = write_process_memory_forced(pid, addr, data, first);
    if (r != 0) return r;
    return write_process_memory_forced(pid, next_page, data + first, len - first);
  }

  /* No kernel_mprotect: mdbg bypasses page protection, and mprotect on wrong or special pages corrupts
   * kernel VM structures. */
  int wrc = mdbg_io_copyin(pid, data, addr, len);
  cr_log("debug", "cheats.mem", "direct_mdbg_write addr=0x%lx len=%zu rc=%d",
         (long)addr, len, wrc);
  if (wrc == 0 && len <= 128) {
    uint8_t vbuf[128];
    if (mdbg_io_copyout(pid, addr, vbuf, len) == 0 && memcmp(vbuf, data, len) != 0)
      cr_log("warn", "cheats.mem",
             "write_verify_mismatch addr=0x%lx len=%zu — bytes did not change after write",
             (long)addr, len);
  }
  return (wrc < 0) ? -1 : 0;
}
