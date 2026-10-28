/* The payload's side of a download (SPEC.md §12): the console is the sender, the engine
 * (ava1::recv, Task 17) the receiver. ava1_send_open walks the source, sends the
 * manifest, waits for the engine's JobMap, then one reader thread reads and one writer
 * thread per live lane sends, so a slow lane never stalls the others. The engine
 * concludes the job (JOB_DONE / JOB_CANCEL). */
#ifndef AVA1_SEND_H
#define AVA1_SEND_H

#include <stddef.h>
#include <stdint.h>

#include "ava1_job.h"

/* The reader shared by downloads and the local copy (Task 19): it walks files in
 * manifest order and produces *encoded* Chunk/Bundle messages through `put`, and the
 * large files' roots through `root`. `put` and `root` are called only from the reader
 * thread. `put` takes ownership of `msg` (frees it) on every return. */
typedef struct {
    ava1_job_t *j;
    const char *src_root;          /* directory, or the file itself when single */
    int single;
    uint32_t cutoff, chunk, bundle;
    const ava1_bits_t *skip;       /* files the receiver already has */
    ava1_rset_t *const *durable;   /* per file id: ranges the receiver has (may be NULL) */
    /* Takes ownership of msg (an encoded Chunk or Bundle); may block for backpressure.
     * Nonzero stops the reader (-ECANCELED = the job is ending: a quiet exit). */
    int (*put)(void *ctx, uint8_t type, uint8_t *msg, size_t len);
    void (*root)(void *ctx, uint32_t id, const uint8_t root[32]);
    void *ctx;
    volatile int *stop;
} ava1_reader_t;

/* ids == NULL: every file (a first pass). Otherwise the named ids, re-read in full. */
int ava1_read_files(ava1_reader_t *r, const uint32_t *ids, uint32_t n);

/* Opens a download job: may_read, stat, the (follow-mode) walk, then a detached job with
 * its thread running (the data layer attaches it and calls ava1_send_start). NULL = the
 * ack carries the refusal (never a partial job). `msg`/`cap` take the human text. */
ava1_job_t *ava1_send_open(const ava1_job_open_t *o, const uint8_t peer[32], ava1_job_open_ack_t *ack, char *msg,
                           size_t cap);
/* Queue the manifest pages; start writers for the lanes already up (post-attach). */
void ava1_send_start(ava1_job_t *j);

/* Tests only (the ctest suite's FFI). Countdowns: the next N lane sends "fail" with
 * AVA1_E_IO before writing anything; the next N writer thread starts "fail". The counter
 * adds every Chunk byte the download sender queues. */
extern uint32_t ava1_send_test_fail_sends;
extern uint32_t ava1_send_test_fail_writer_starts;
extern uint64_t ava1_send_test_chunk_bytes;
/* Whether the per-download stage-timer line is on (flag file or PS5UPLOAD_AVA1_TIMING). */
int ava1_send_timing_enabled(void);
/* Tests only: one sender job's window and queues, driven step by step (no threads). */
void ava1_send_test_begin(uint64_t credit);
void ava1_send_test_end(void);
int ava1_send_test_put(uint64_t len);
uint32_t ava1_send_test_take(uint16_t lane);
int ava1_send_test_settle(uint32_t seq, int rc);
void ava1_send_test_lane(uint16_t lane, int up);
void ava1_send_test_received(uint32_t seq);
void ava1_send_test_credit(uint64_t n);
void ava1_send_test_stopping(void);
void ava1_send_test_state(uint64_t out[4]);

#endif
