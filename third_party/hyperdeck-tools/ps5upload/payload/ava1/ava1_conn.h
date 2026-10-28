/* Frames on a socket, sealed after the handshake (SPEC.md §2, §4.4). One reader
 * thread per connection; writers (that thread, RPC workers, job threads and the post
 * queue's writer) share `wmu`. */
#ifndef AVA1_CONN_H
#define AVA1_CONN_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "ava1_frame.h"

/* ava1_conn_post's queue: at most this many frames (or bytes) queued — a frame the
 * writer has already taken to send does not count. A full queue breaks the
 * connection (the peer is not draining, so it is of no use). */
#define AVA1_Q_ENTRIES 64u
#define AVA1_Q_BYTES (4u * 1024u * 1024u)

/* One frame queued by ava1_conn_post: `len` body bytes follow the struct. */
typedef struct ava1_qitem {
    struct ava1_qitem *next;
    uint8_t type;
    uint8_t flags;
    uint32_t channel;
    size_t len;
} ava1_qitem_t;

/* The first counter value no frame may be sealed or opened with. A direction's counter is the
 * AEAD nonce (SPEC.md 4.4); a wrap would reuse nonces under one key. 2^64 frames are
 * unreachable, so reaching this breaks the connection cleanly (review 006 #1). */
#define AVA1_NONCE_CEILING (UINT64_MAX - 1)

typedef struct {
    int fd;
    int keyed;
    uint8_t send_key[32];
    uint8_t recv_key[32];
    uint64_t send_ctr;
    uint64_t recv_ctr;
    pthread_mutex_t wmu;
    /* Monotonic ms after which reads fail with AVA1_E_TIMEOUT; 0 = none. Bounds the whole
     * handshake, which SO_RCVTIMEO alone cannot (it restarts on every byte). */
    uint64_t deadline_ms;
    /* A write failed part-way (or after sealing): the stream is torn, so nothing more
     * is sent. Read and written from several threads. */
    _Atomic int broken;
    /* Liveness after the handshake (SPEC.md §6). Any byte received is proof of life, so a
     * large frame on a slow link never looks dead; set by the reader before serving. */
    uint32_t idle_ms;     /* a read with no byte for this long fails (AVA1_E_TIMEOUT); 0 = none */
    uint32_t min_rate;    /* bytes/s floor for one frame, after an idle_ms grace; 0 = none */
    uint64_t last_rx_ms;  /* monotonic time of the last byte received */
    /* While a read waits it calls tick at least every tick_ms (a due Ping, a stop check);
     * nonzero from tick ends the read with AVA1_E_CLOSED. Reader thread only. */
    int (*tick)(void *arg);
    void *tick_arg;
    uint32_t tick_ms;
    /* Writes: a send with no progress for send_idle_ms, or slower than min_rate over the
     * frame, fails and breaks the connection; 0 = blocking (SO_SNDTIMEO applies). */
    uint32_t send_idle_ms;
    /* The header being read: the AEAD's associated data (ava1_conn_recv_header /
     * ava1_conn_recv_body). Reader thread only. */
    uint8_t rx_hdr[AVA1_HEADER_LEN];
    /* The bounded queue ava1_conn_post enqueues on and a lazily started writer thread
     * drains with ava1_conn_send_flags, so a reader thread never waits on a socket.
     * ava1_conn_destroy closes the queue (dropping pending frames) and joins the thread. */
    pthread_mutex_t qmu;
    pthread_cond_t qcv;
    pthread_t q_thread;
    int q_started; /* the writer thread is draining */
    int q_closed;  /* no more posts: the writer drops pending items and exits */
    int q_busy;    /* the writer is sending a frame it popped */
    unsigned q_n;
    size_t q_bytes;
    ava1_qitem_t *q_head, *q_tail;
} ava1_conn_t;

/* CLOCK_MONOTONIC in milliseconds. */
uint64_t ava1_now_ms(void);

void ava1_conn_init(ava1_conn_t *c, int fd);
/* Wipes keys, ends the post queue's writer thread and destroys the locks; does not
 * close fd — close it only after this returns, so the writer cannot touch an fd
 * number that has been reused. */
void ava1_conn_destroy(ava1_conn_t *c);
int ava1_conn_send(ava1_conn_t *c, uint8_t type, uint32_t channel, const uint8_t *body, size_t len);
/* Like send, with frame flags of the caller's choosing. */
int ava1_conn_send_flags(ava1_conn_t *c, uint8_t type, uint8_t flags, uint32_t channel,
                         const uint8_t *body, size_t len);
/* Like send, but returns AVA1_E_BUSY instead of waiting when another writer holds the
 * connection: for liveness Pings and Pongs, which data in flight makes unnecessary. */
int ava1_conn_try_send(ava1_conn_t *c, uint8_t type, uint32_t channel, const uint8_t *body, size_t len);
/* Sends `frame` in place: AVA1_HEADER_LEN bytes of header room, the body, then
 * AVA1_TAG_LEN bytes of tag room; the header and (keyed) the seal land in the buffer,
 * and the whole frame is written once. */
int ava1_conn_send_frame(ava1_conn_t *c, uint8_t type, uint32_t channel, uint8_t *frame, size_t body_len);
/* The header of one frame; *body_len is its body length (the MAC is not counted).
 * Reader thread only. AVA1_E_* on failure. */
/* Lane socket buffers (review 003 section 1): ask for AVA1_LANE_SOCKBUF on SO_RCVBUF and
 * SO_SNDBUF so a lane thread that is busy opening a frame for 10-20 ms does not close the
 * sender's TCP window. Call before the first read. The kernel may cap the request (it takes
 * what it gives); the effective sizes are returned (0 when unreadable). Returns 0, or -1 when
 * neither option could be set. */
#define AVA1_LANE_SOCKBUF (4 * 1024 * 1024)
int ava1_conn_tune_buffers(int fd, int *rcv, int *snd);
int ava1_conn_recv_header(ava1_conn_t *c, ava1_header_t *h, size_t *body_len);
/* The body of the frame whose header ava1_conn_recv_header just read (into buf;
 * keyed: opened in place). Bounded by the same liveness as a single read. Reader
 * thread only. */
int ava1_conn_recv_body(ava1_conn_t *c, uint8_t *buf, size_t body_len);
/* Reads one frame; the opened body goes to buf. Reader thread only. AVA1_E_* on failure. */
int ava1_conn_recv(ava1_conn_t *c, uint8_t *type, uint8_t *flags, uint32_t *channel, uint8_t *buf,
                   size_t cap, size_t *len);
/* The non-blocking send: copies `body` onto the connection's bounded writer queue and
 * returns at once (never waits on the socket, so a reader thread may call it). A full
 * queue returns AVA1_E_BUSY and breaks the connection. */
int ava1_conn_post(ava1_conn_t *c, uint8_t type, uint8_t flags, uint32_t channel, const uint8_t *body,
                   size_t len);
/* Waits up to `ms` for the post queue to be written out (a closing connection's last
 * words, such as an Error, go out before it is shut down). */
void ava1_conn_drain(ava1_conn_t *c, uint32_t ms);

#endif
