/* AVA1 range sets and file bits — which parts of which files are already durable
 * (SPEC.md §14). Plain in-memory structures: no sockets, no Sony APIs, no locking. */
#ifndef AVA1_RANGES_H
#define AVA1_RANGES_H

#include <stddef.h>
#include <stdint.h>

#include "ava1_wire.h"

/* A set of [start, end) byte ranges, stored as n pairs in v: sorted, merged and
 * disjoint. Zero-initialise before the first ava1_rset_add; release with
 * ava1_rset_clear. */
typedef struct {
    uint64_t *v;
    size_t n, cap; /* n pairs [v[2i], v[2i+1]) */
} ava1_rset_t;

int ava1_rset_add(ava1_rset_t *r, uint64_t start, uint64_t end); /* 0, or -1 out of memory */
uint64_t ava1_rset_covered(const ava1_rset_t *r);
int ava1_rset_covers(const ava1_rset_t *r, uint64_t start, uint64_t end);
void ava1_rset_clear(ava1_rset_t *r); /* frees */

/* A bitset of file ids; ava1_bits_init sets every field, ava1_bits_free releases. */
typedef struct {
    uint64_t *w;
    uint32_t n;
} ava1_bits_t;

int ava1_bits_init(ava1_bits_t *b, uint32_t n);
void ava1_bits_set(ava1_bits_t *b, uint32_t i);
void ava1_bits_clear(ava1_bits_t *b, uint32_t i);
int ava1_bits_get(const ava1_bits_t *b, uint32_t i);
uint32_t ava1_bits_count(const ava1_bits_t *b);
void ava1_bits_free(ava1_bits_t *b);
/* Appends FileRun records for the set bits to `blob`; 0 or the writer's error. */
int ava1_bits_append_runs(const ava1_bits_t *b, ava1_w_t *blob);

#endif
