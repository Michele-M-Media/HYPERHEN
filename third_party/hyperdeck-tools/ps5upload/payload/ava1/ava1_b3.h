/* BLAKE3 verification groups (SPEC.md §13) over the vendored BLAKE3 C. */
#ifndef AVA1_B3_H
#define AVA1_B3_H

#include <stddef.h>
#include <stdint.h>

#include "ava1_gen.h" /* AVA1_GROUP_SHIFT */

/* One verification group in bytes: 1 MiB. */
#define AVA1_GROUP_LEN (1ULL << AVA1_GROUP_SHIFT)

/* Chaining value of group `index` (non-root). `len` is AVA1_GROUP_LEN, or less only for
 * the file's last group; > 0. Uses ~41 KiB of stack (the 32 KiB CV array plus BLAKE3's
 * own frames) — call it only from threads sized for that (the 256 KiB management
 * threads), never from the 64 KiB connection threads. */
void ava1_b3_group_cv(const uint8_t *data, size_t len, uint64_t index, uint8_t cv[32]);
/* The root of a file of n >= 2 groups from their CVs. */
void ava1_b3_root_from_cvs(const uint8_t (*cvs)[32], uint64_t n, uint8_t root[32]);
/* Plain BLAKE3 (files of zero or one group). */
void ava1_b3_hash(const uint8_t *data, size_t len, uint8_t out[32]);

#endif
