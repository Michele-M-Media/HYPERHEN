#include "ava1_ranges.h"

#include <stdlib.h>
#include <string.h>

#include "ava1_gen.h"

int ava1_rset_add(ava1_rset_t *r, uint64_t start, uint64_t end) {
    size_t i = 0, j;
    uint64_t s = start, e = end;
    if (start >= end) return 0;
    while (i < r->n && r->v[2 * i + 1] < s) i++;
    j = i;
    while (j < r->n && r->v[2 * j] <= e) {
        if (r->v[2 * j] < s) s = r->v[2 * j];
        if (r->v[2 * j + 1] > e) e = r->v[2 * j + 1];
        j++;
    }
    if (j == i) { /* insert a new pair at i */
        if (r->n == r->cap) {
            size_t cap = r->cap ? r->cap * 2 : 4;
            uint64_t *v = realloc(r->v, cap * 2 * sizeof *v);
            if (!v) return -1;
            r->v = v;
            r->cap = cap;
        }
        memmove(&r->v[2 * (i + 1)], &r->v[2 * i], (r->n - i) * 2 * sizeof *r->v);
        r->n++;
    } else if (j > i + 1) { /* pairs i..j collapse into i */
        memmove(&r->v[2 * (i + 1)], &r->v[2 * j], (r->n - j) * 2 * sizeof *r->v);
        r->n -= j - i - 1;
    }
    r->v[2 * i] = s;
    r->v[2 * i + 1] = e;
    return 0;
}

uint64_t ava1_rset_covered(const ava1_rset_t *r) {
    uint64_t t = 0;
    size_t i;
    for (i = 0; i < r->n; i++) t += r->v[2 * i + 1] - r->v[2 * i];
    return t;
}

int ava1_rset_covers(const ava1_rset_t *r, uint64_t start, uint64_t end) {
    size_t i;
    for (i = 0; i < r->n; i++)
        if (r->v[2 * i] <= start && end <= r->v[2 * i + 1]) return 1;
    return 0;
}

void ava1_rset_clear(ava1_rset_t *r) {
    free(r->v);
    memset(r, 0, sizeof *r);
}

int ava1_bits_init(ava1_bits_t *b, uint32_t n) {
    b->n = n;
    b->w = calloc((size_t)n / 64 + 1, sizeof *b->w);
    return b->w ? 0 : -1;
}

void ava1_bits_set(ava1_bits_t *b, uint32_t i) {
    if (i < b->n) b->w[i / 64] |= 1ull << (i % 64);
}

void ava1_bits_clear(ava1_bits_t *b, uint32_t i) {
    if (i < b->n) b->w[i / 64] &= ~(1ull << (i % 64));
}

int ava1_bits_get(const ava1_bits_t *b, uint32_t i) { return i < b->n && (b->w[i / 64] >> (i % 64)) & 1; }

uint32_t ava1_bits_count(const ava1_bits_t *b) {
    /* A zeroed bits_t is a valid empty set everywhere else in this file; keep that
     * symmetry instead of dereferencing a NULL word. */
    if (!b->w) return 0;
    uint32_t i, c = 0;
    for (i = 0; i < b->n / 64 + 1; i++) c += (uint32_t)__builtin_popcountll(b->w[i]);
    return c;
}

void ava1_bits_free(ava1_bits_t *b) {
    free(b->w);
    memset(b, 0, sizeof *b);
}

int ava1_bits_append_runs(const ava1_bits_t *b, ava1_w_t *blob) {
    uint32_t i = 0;
    while (i < b->n) {
        ava1_file_run_t r;
        if (!ava1_bits_get(b, i)) {
            i++;
            continue;
        }
        memset(&r, 0, sizeof r);
        r.first = i;
        while (i < b->n && ava1_bits_get(b, i)) i++;
        r.count = i - r.first;
        if (ava1_file_run_append(blob, &r) != 0) return blob->err ? blob->err : -1;
    }
    return blob->err;
}
