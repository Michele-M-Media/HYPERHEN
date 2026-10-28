#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CR_RELOC_CAPACITY 256

/* Copies whole instructions covering at least min_len bytes of src into dst, fixes
 * rip-relative operands and relative branches, then appends a jump back to src.
 * On failure `why` says which instruction rule stopped it. */
bool cr_relocate(const uint8_t *src, uint64_t src_addr, uint8_t *dst, uint64_t dst_addr,
                 size_t min_len, size_t cap, size_t *src_len, const char **why);
