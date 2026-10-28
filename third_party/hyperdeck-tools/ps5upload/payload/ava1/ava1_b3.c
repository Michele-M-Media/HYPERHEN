#include "ava1_b3.h"

#include <assert.h>
#include <string.h>

#include "blake3.h"
#include "blake3_impl.h"

#define CHUNKS_PER_GROUP (AVA1_GROUP_LEN / BLAKE3_CHUNK_LEN)

static const uint32_t B3_IV[8] = { 0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
                                   0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL };

static void words_to_bytes(const uint32_t w[8], uint8_t out[32]) {
    unsigned i;
    for (i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)w[i];
        out[4 * i + 1] = (uint8_t)(w[i] >> 8);
        out[4 * i + 2] = (uint8_t)(w[i] >> 16);
        out[4 * i + 3] = (uint8_t)(w[i] >> 24);
    }
}

/* CV of the file's last chunk when it is shorter than a full chunk. */
static void partial_chunk_cv(const uint8_t *p, size_t len, uint64_t counter, uint8_t out[32]) {
    uint32_t cv[8];
    uint8_t block[BLAKE3_BLOCK_LEN];
    size_t off = 0;
    uint8_t flags = CHUNK_START;
    memcpy(cv, B3_IV, sizeof cv);
    for (;;) {
        size_t n = len - off > BLAKE3_BLOCK_LEN ? BLAKE3_BLOCK_LEN : len - off;
        int last = off + n >= len;
        memset(block, 0, sizeof block);
        memcpy(block, p + off, n);
        blake3_compress_in_place(cv, block, (uint8_t)n, counter, (uint8_t)(flags | (last ? CHUNK_END : 0)));
        if (last) break;
        off += n;
        flags = 0;
    }
    words_to_bytes(cv, out);
}

static void parent_cv(const uint8_t l[32], const uint8_t r[32], uint8_t flags, uint8_t out[32]) {
    uint32_t cv[8];
    uint8_t block[BLAKE3_BLOCK_LEN];
    memcpy(cv, B3_IV, sizeof cv);
    memcpy(block, l, 32);
    memcpy(block + 32, r, 32);
    blake3_compress_in_place(cv, block, BLAKE3_BLOCK_LEN, 0, (uint8_t)(PARENT | flags));
    words_to_bytes(cv, out);
}

static uint64_t largest_pow2_below(uint64_t n) { /* n >= 2 */
    uint64_t p = 1;
    while (p * 2 < n) p *= 2;
    return p;
}

/* Merges cvs[0..n) (n >= 1) along BLAKE3's tree; non-root. */
static void merge(const uint8_t (*cvs)[32], uint64_t n, uint8_t out[32]) {
    uint8_t l[32], r[32];
    uint64_t left;
    if (n == 1) {
        memcpy(out, cvs[0], 32);
        return;
    }
    left = largest_pow2_below(n);
    merge(cvs, left, l);
    merge(cvs + left, n - left, r);
    parent_cv(l, r, 0, out);
}

void ava1_b3_group_cv(const uint8_t *data, size_t len, uint64_t index, uint8_t cv[32]) {
    uint8_t cvs[CHUNKS_PER_GROUP][32];       /* 32 KiB */
    const uint8_t *inputs[CHUNKS_PER_GROUP]; /* 8 KiB */
    size_t full = len / BLAKE3_CHUNK_LEN, tail = len % BLAKE3_CHUNK_LEN, i, n;
    uint64_t base = index * CHUNKS_PER_GROUP;
    for (i = 0; i < full; i++) inputs[i] = data + i * BLAKE3_CHUNK_LEN;
    if (full) {
        blake3_hash_many(inputs, full, BLAKE3_CHUNK_LEN / BLAKE3_BLOCK_LEN, B3_IV, base, true, 0, CHUNK_START,
                         CHUNK_END, &cvs[0][0]);
    }
    n = full;
    if (tail) partial_chunk_cv(data + full * BLAKE3_CHUNK_LEN, tail, base + full, cvs[n++]);
    merge((const uint8_t (*)[32])cvs, n, cv);
}

void ava1_b3_root_from_cvs(const uint8_t (*cvs)[32], uint64_t n, uint8_t root[32]) {
    /* The documented precondition. Without it a one-CV call would recurse into
     * merge() with n = UINT64_MAX and read past the array (Rust panics here). */
    assert(n >= 2);
    uint8_t l[32], r[32];
    uint64_t left = largest_pow2_below(n);
    merge(cvs, left, l);
    merge(cvs + left, n - left, r);
    parent_cv(l, r, ROOT, root);
}

void ava1_b3_hash(const uint8_t *data, size_t len, uint8_t out[32]) {
    blake3_hasher h;
    blake3_hasher_init(&h);
    blake3_hasher_update(&h, data, len);
    blake3_hasher_finalize(&h, out, 32);
}
