/* The 16-byte AVA1 frame header (SPEC.md §2). */
#ifndef AVA1_FRAME_H
#define AVA1_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define AVA1_HEADER_LEN 16
#define AVA1_TAG_LEN 16
#define AVA1_MAX_BODY (16u * 1024u * 1024u)
#define AVA1_FLAG_SEALED 0x01u
#define AVA1_FLAG_IGNORABLE 0x02u

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t channel;
    uint32_t body_len;
} ava1_header_t;

uint32_t ava1_crc32c(const uint8_t *p, size_t n);
void ava1_header_encode(const ava1_header_t *h, uint8_t out[AVA1_HEADER_LEN]);
/* 0, AVA1_E_MAGIC, AVA1_E_CRC or AVA1_E_TOOLONG. */
int ava1_header_decode(const uint8_t in[AVA1_HEADER_LEN], ava1_header_t *h);

/* Lane frame-buffer pool (review 003 section 3). A lane body is read into a buffer from
 * ava1_frame_alloc. A body whose length is a size class (1, 4, 8, 15 or 16 MiB) plus at most a
 * chunk's few header bytes comes from that class's free list and is kept for the next frame
 * instead of being returned to the allocator, which hands a fresh 15 MiB block back to the
 * kernel on free and faults it in again on the next malloc. Buffers are reused without zeroing.
 * Any other length is allocated exactly, so memory in use is the credit window's byte count
 * (plus under 0.4% slack on pooled frames), never a rounded-up class. A class holds at most
 * budget/class idle buffers and all classes together at most `budget` bytes. Every buffer is
 * counted from alloc to free, pooled or not, so the counters can back a leak check. */
#define AVA1_FRAME_CLASSES 5
/* The capacity to hand ava1_frame_free for a body of `len` bytes: the class size when pooled,
 * else `len` (1 for 0). Deterministic, so a holder that knows only the length can free. */
size_t ava1_frame_cap(size_t len);
/* A buffer of at least `len` bytes; *cap (optional) = ava1_frame_cap(len). NULL when out of
 * memory. */
void *ava1_frame_alloc(size_t len, size_t *cap);
/* Returns a buffer from ava1_frame_alloc with its cap. A second free of a buffer the pool
 * already holds is refused (returns -1) rather than corrupting the list. */
int ava1_frame_free(void *p, size_t cap);
/* The idle-memory ceiling (0 = 96 MiB, the default admit budget); trims the pool to fit. */
void ava1_frame_pool_set_budget(uint64_t bytes);
/* Idle buffers of class i (0..4), buffers handed out and not yet freed, a full drain, and
 * the bytes handed out now and at their peak (reset_peak rebases the peak to now). */
size_t ava1_frame_pool_idle(int cls);
size_t ava1_frame_pool_outstanding(void);
uint64_t ava1_frame_pool_outstanding_bytes(void);
uint64_t ava1_frame_pool_peak_bytes(void);
void ava1_frame_pool_reset_peak(void);
void ava1_frame_pool_trim(void);

#endif
