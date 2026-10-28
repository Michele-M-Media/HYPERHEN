/* AVA1 receiver (SPEC.md §11.3–11.6, §13.4, §14, §15): JobOpen, the manifest pages,
 * prepare (policies, staging), journal replay and resume, and the JobMap it answers.
 * The apply engine (ava1_apply.h) does everything after the map. */
#ifndef AVA1_RECV_H
#define AVA1_RECV_H

#include <stddef.h>
#include <stdint.h>

#include "ava1_gen.h"
#include "ava1_job.h"

typedef struct {
    uint8_t id[16], owner[32];
    uint8_t kind, policy;
    uint32_t flags;
    uint32_t entries; /* the manifest's entry count when JobOpen carries it; 0 = not known */
    const char *root;
    ava1_emit_fn emit;
    void *emit_ctx;
    const uint8_t *sid; /* a new job is attached to this session as it is created (Task 14); NULL = none */
} ava1_recv_spec_t;

/* JobOpen. Returns the referenced job (new, resumed from disk, or re-attached from memory)
 * with *ack filled; NULL with ack->status set (and `msg` saying why) on refusal. */
ava1_job_t *ava1_recv_open(const ava1_recv_spec_t *s, ava1_job_open_ack_t *ack, char *msg, size_t cap);
int ava1_recv_page(ava1_job_t *j, const ava1_manifest_page_t *p); /* reader thread; cheap */
int ava1_recv_end(ava1_job_t *j, const ava1_manifest_end_t *e);   /* queues prepare + map */
int ava1_recv_resume(ava1_job_t *j, const uint8_t hash[32]);      /* queues the fast-path map */
void ava1_recv_cancel(ava1_job_t *j);                             /* stop; the journal stays */

#endif
