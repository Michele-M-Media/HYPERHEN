#ifndef SM_PKG_STREAM_H
#define SM_PKG_STREAM_H
#include <stddef.h>
#include <stdint.h>

#define SM_PKG_STREAM_CHUNK (1024u * 1024u)
#define SM_PKG_STREAM_CLIENTS 4u
typedef struct sm_pkg_stream sm_pkg_stream_t;
typedef struct { uint64_t id, offset; size_t size; } sm_pkg_range_t;
sm_pkg_stream_t *sm_pkg_stream_create(uint64_t total);
void sm_pkg_stream_retain(sm_pkg_stream_t *stream);
void sm_pkg_stream_release(sm_pkg_stream_t *stream);
void sm_pkg_stream_stop(sm_pkg_stream_t *stream);
// First terminal status wins; cancellation and source failure remain distinct.
void sm_pkg_stream_fail(sm_pkg_stream_t *stream, int status);
int sm_pkg_stream_status(sm_pkg_stream_t *stream);
int sm_pkg_stream_read(void *ctx, uint64_t offset, void *buffer, size_t size);
// One outstanding request is claimed by one browser poller. Empty poll: EAGAIN.
int sm_pkg_stream_request(sm_pkg_stream_t *stream, sm_pkg_range_t *range);
// Never blocks an HTTP worker. A stable client can reclaim an unacknowledged range.
int sm_pkg_stream_try_request(sm_pkg_stream_t *stream, unsigned client, sm_pkg_range_t *range);
int sm_pkg_stream_reply(sm_pkg_stream_t *stream, uint64_t id,
                        const void *data, size_t size);
int sm_pkg_stream_wakeup_fd(sm_pkg_stream_t *stream);
#endif
