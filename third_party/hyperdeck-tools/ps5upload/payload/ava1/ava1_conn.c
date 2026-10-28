#include "ava1_conn.h"

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "ava1_frame.h"
#include "ava1_noise.h"
#include "ava1_platform.h"
#include "ava1_wire.h"
#include "monocypher.h"

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

uint64_t ava1_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ms until `t`, at least 1 and at most INT_MAX-ish; `t` must be in the future. */
static int ms_until(uint64_t t, uint64_t now) {
    uint64_t d = t - now;
    return d > 0x3fffffffu ? 0x3fffffff : (d == 0 ? 1 : (int)d);
}

/* A frame's whole-body deadline under the rate floor: grace + len / min_rate. */
static uint64_t rate_deadline(uint64_t now, uint32_t grace_ms, uint32_t min_rate, size_t len) {
    return now + grace_ms + (uint64_t)len * 1000u / min_rate;
}

static int write_all(ava1_conn_t *c, const uint8_t *p, size_t n, size_t *sent) {
    int fd = c->fd;
    uint64_t deadline = 0;
    if (c->send_idle_ms && c->min_rate) deadline = rate_deadline(ava1_now_ms(), c->send_idle_ms, c->min_rate, n);
    while (n > 0) {
        ssize_t k;
        int flags = SEND_FLAGS;
        if (c->send_idle_ms) {
            /* Never block in send(): wait for room with a bound, then send what fits. */
            struct pollfd pf;
            uint64_t now = ava1_now_ms();
            int wait = (int)c->send_idle_ms, pr;
            if (deadline) {
                if (now >= deadline) return AVA1_E_TIMEOUT;
                if (ms_until(deadline, now) < wait) wait = ms_until(deadline, now);
            }
            pf.fd = fd;
            pf.events = POLLOUT;
            pf.revents = 0;
            pr = poll(&pf, 1, wait);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return AVA1_E_IO;
            }
            if (pr == 0) return AVA1_E_TIMEOUT; /* the peer stopped reading */
            flags |= MSG_DONTWAIT;
        }
        k = send(fd, p, n, flags);
        if (k < 0) {
            if (errno == EINTR) continue;
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && c->send_idle_ms) continue;
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? AVA1_E_TIMEOUT : AVA1_E_IO;
        }
        p += k;
        n -= (size_t)k;
        *sent += (size_t)k;
    }
    return 0;
}

/* 1 if a byte (or EOF) is waiting right now, 0 if not, <0 on error. */
static int readable_now(int fd) {
    struct pollfd pf;
    int pr;
    pf.fd = fd;
    pf.events = POLLIN;
    pf.revents = 0;
    do {
        pr = poll(&pf, 1, 0);
    } while (pr < 0 && errno == EINTR);
    return pr;
}

/* Reads exactly n bytes. Bounded by deadline_ms (absolute, the handshake or a frame's
 * rate floor) and idle_ms (since the last byte); calls tick while it waits. */
static int read_all(ava1_conn_t *c, uint8_t *p, size_t n) {
    int fd = c->fd;
    while (n > 0) {
        ssize_t k;
        if (c->deadline_ms || c->idle_ms || c->tick) {
            struct pollfd pf;
            uint64_t now = ava1_now_ms();
            int wait = -1, pr;
            if (c->deadline_ms) {
                if (now >= c->deadline_ms) return AVA1_E_TIMEOUT;
                wait = ms_until(c->deadline_ms, now);
            }
            if (c->idle_ms) {
                uint64_t dead = c->last_rx_ms + c->idle_ms;
                if (now >= dead) {
                    /* The limit can pass while this thread is away from the socket (waiting
                     * to write, writing a reply): the peer's bytes may be waiting unread.
                     * Silence is only what nothing to read right now proves. */
                    int ready = readable_now(fd);
                    if (ready < 0) return AVA1_E_IO;
                    if (ready == 0) return AVA1_E_TIMEOUT;
                    goto take;
                }
                if (wait < 0 || ms_until(dead, now) < wait) wait = ms_until(dead, now);
            }
            if (c->tick && (wait < 0 || (int)c->tick_ms < wait)) wait = c->tick_ms ? (int)c->tick_ms : 1;
            pf.fd = fd;
            pf.events = POLLIN;
            pf.revents = 0;
            pr = poll(&pf, 1, wait);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return AVA1_E_IO;
            }
            if (c->tick && c->tick(c->tick_arg) != 0) return AVA1_E_CLOSED;
            if (pr == 0) continue; /* the limits are checked at the top */
        }
    take:
        k = recv(fd, p, n, 0);
        if (k == 0) return AVA1_E_CLOSED;
        if (k < 0) {
            if (errno == EINTR) continue;
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? AVA1_E_TIMEOUT : AVA1_E_IO;
        }
        c->last_rx_ms = ava1_now_ms();
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

void ava1_conn_init(ava1_conn_t *c, int fd) {
    memset(c, 0, sizeof *c);
    c->fd = fd;
    pthread_mutex_init(&c->wmu, NULL);
    pthread_mutex_init(&c->qmu, NULL);
    pthread_cond_init(&c->qcv, NULL);
}

void ava1_conn_destroy(ava1_conn_t *c) {
    if (c->q_started) {
        pthread_mutex_lock(&c->qmu);
        c->q_closed = 1;
        pthread_cond_broadcast(&c->qcv);
        pthread_mutex_unlock(&c->qmu);
        /* The writer's send in flight is bounded (pacing, and the socket has been shut
         * down before this connection is freed), so the join cannot wedge. */
        pthread_join(c->q_thread, NULL);
    }
    pthread_cond_destroy(&c->qcv);
    pthread_mutex_destroy(&c->qmu);
    pthread_mutex_destroy(&c->wmu);
    crypto_wipe(c->send_key, sizeof c->send_key);
    crypto_wipe(c->recv_key, sizeof c->recv_key);
}

/* Caller holds wmu. `frame` = AVA1_HEADER_LEN bytes of header room, the body, then
 * AVA1_TAG_LEN bytes of tag room; the header and (keyed) the seal land in place and
 * the whole frame is written once. */
static int send_frame_locked(ava1_conn_t *c, uint8_t type, uint8_t flags, uint32_t channel, uint8_t *frame,
                             size_t len) {
    ava1_header_t h;
    size_t mac = c->keyed ? AVA1_TAG_LEN : 0, total = AVA1_HEADER_LEN + len + mac;
    size_t sent = 0;
    int rc;
    if (c->broken) return AVA1_E_IO;
    if (len + mac > AVA1_MAX_BODY) return AVA1_E_TOOLONG;
    h.type = type;
    h.flags = (uint8_t)(flags | (c->keyed ? AVA1_FLAG_SEALED : 0));
    h.channel = channel;
    h.body_len = (uint32_t)(len + mac);
    ava1_header_encode(&h, frame);
    if (c->keyed && c->send_ctr >= AVA1_NONCE_CEILING) {
        /* Nonce space exhausted: never seal again under this key. */
        c->broken = 1;
        shutdown(c->fd, SHUT_RDWR);
        return AVA1_E_IO;
    }
    if (c->keyed) {
        ava1_seal(c->send_key, c->send_ctr++, frame, 12, frame + AVA1_HEADER_LEN, len,
                  frame + AVA1_HEADER_LEN + len);
    }
    rc = write_all(c, frame, total, &sent);
    if (rc != 0 && (sent > 0 || c->keyed)) {
        /* Half a frame, or a counter spent on a frame the peer never got. */
        c->broken = 1;
        shutdown(c->fd, SHUT_RDWR);
    }
    return rc;
}

/* Caller holds wmu. */
static int send_locked(ava1_conn_t *c, uint8_t type, uint8_t flags, uint32_t channel, const uint8_t *body,
                       size_t len) {
    uint8_t *frame;
    int rc;
    if (len + (c->keyed ? AVA1_TAG_LEN : 0) > AVA1_MAX_BODY) return AVA1_E_TOOLONG;
    frame = malloc(AVA1_HEADER_LEN + len + AVA1_TAG_LEN);
    if (!frame) return AVA1_E_IO;
    if (len) memcpy(frame + AVA1_HEADER_LEN, body, len);
    rc = send_frame_locked(c, type, flags, channel, frame, len);
    free(frame);
    return rc;
}

int ava1_conn_send(ava1_conn_t *c, uint8_t type, uint32_t channel, const uint8_t *body, size_t len) {
    return ava1_conn_send_flags(c, type, 0, channel, body, len);
}

int ava1_conn_send_flags(ava1_conn_t *c, uint8_t type, uint8_t flags, uint32_t channel, const uint8_t *body,
                         size_t len) {
    int rc;
    pthread_mutex_lock(&c->wmu);
    rc = send_locked(c, type, flags, channel, body, len);
    pthread_mutex_unlock(&c->wmu);
    return rc;
}

int ava1_conn_try_send(ava1_conn_t *c, uint8_t type, uint32_t channel, const uint8_t *body, size_t len) {
    int rc;
    if (pthread_mutex_trylock(&c->wmu) != 0) return AVA1_E_BUSY;
    rc = send_locked(c, type, 0, channel, body, len);
    pthread_mutex_unlock(&c->wmu);
    return rc;
}

int ava1_conn_send_frame(ava1_conn_t *c, uint8_t type, uint32_t channel, uint8_t *frame, size_t body_len) {
    int rc;
    pthread_mutex_lock(&c->wmu);
    rc = send_frame_locked(c, type, 0, channel, frame, body_len);
    pthread_mutex_unlock(&c->wmu);
    return rc;
}

int ava1_conn_recv_header(ava1_conn_t *c, ava1_header_t *h, size_t *body_len) {
    int rc = read_all(c, c->rx_hdr, sizeof c->rx_hdr);
    if (rc != 0) return rc;
    rc = ava1_header_decode(c->rx_hdr, h);
    if (rc != 0) return rc;
    if (c->keyed) {
        if (!(h->flags & AVA1_FLAG_SEALED) || h->body_len < AVA1_TAG_LEN) return AVA1_E_TAG;
        *body_len = h->body_len - AVA1_TAG_LEN;
    } else {
        if (h->flags & AVA1_FLAG_SEALED) return AVA1_E_PROTO;
        *body_len = h->body_len;
    }
    return 0;
}

int ava1_conn_recv_body(ava1_conn_t *c, uint8_t *buf, size_t body_len) {
    uint8_t mac[AVA1_TAG_LEN];
    uint64_t saved = c->deadline_ms;
    int rc;
    /* Rate floor: a peer may be slow but must not drip one frame forever. */
    if (c->min_rate && c->idle_ms) {
        uint64_t d = rate_deadline(ava1_now_ms(), c->idle_ms, c->min_rate,
                                   body_len + (c->keyed ? AVA1_TAG_LEN : 0));
        if (!c->deadline_ms || d < c->deadline_ms) c->deadline_ms = d;
    }
    rc = read_all(c, buf, body_len);
    if (rc == 0 && c->keyed) rc = read_all(c, mac, sizeof mac);
    c->deadline_ms = saved;
    if (rc != 0) return rc;
    if (c->keyed) {
        if (c->recv_ctr >= AVA1_NONCE_CEILING) return AVA1_E_TAG; /* never open past the ceiling */
        if (ava1_open(c->recv_key, c->recv_ctr, c->rx_hdr, 12, buf, body_len, mac) != 0) return AVA1_E_TAG;
        c->recv_ctr++;
    }
    return 0;
}

int ava1_conn_recv(ava1_conn_t *c, uint8_t *type, uint8_t *flags, uint32_t *channel, uint8_t *buf,
                   size_t cap, size_t *len) {
    ava1_header_t h;
    size_t body;
    int rc = ava1_conn_recv_header(c, &h, &body);
    if (rc != 0) return rc;
    if (body > cap) return AVA1_E_TOOLONG;
    rc = ava1_conn_recv_body(c, buf, body);
    if (rc != 0) return rc;
    *type = h.type;
    *flags = h.flags;
    *channel = h.channel;
    *len = body;
    return 0;
}

/* The post queue's writer: pops one frame at a time and sends it with the waiting
 * send, so the queue's order is the wire's order. Ends when the queue is closed and
 * drained (pending frames are dropped: the connection is over). */
static void *q_writer(void *arg) {
    ava1_conn_t *c = arg;
    for (;;) {
        ava1_qitem_t *it = NULL;
        int drop = 0;
        pthread_mutex_lock(&c->qmu);
        while (!c->q_closed && !c->q_head) pthread_cond_wait(&c->qcv, &c->qmu);
        drop = c->q_closed;
        if (c->q_head) {
            it = c->q_head;
            c->q_head = it->next;
            if (!c->q_head) c->q_tail = NULL;
            c->q_n--;
            c->q_bytes -= it->len;
            c->q_busy = 1;
        }
        pthread_mutex_unlock(&c->qmu);
        if (!it) break;
        if (drop) {
            free(it);
        } else {
            (void)ava1_conn_send_flags(c, it->type, it->flags, it->channel, (const uint8_t *)(it + 1), it->len);
            free(it);
        }
        pthread_mutex_lock(&c->qmu);
        c->q_busy = 0;
        pthread_mutex_unlock(&c->qmu);
    }
    return NULL;
}

int ava1_conn_post(ava1_conn_t *c, uint8_t type, uint8_t flags, uint32_t channel, const uint8_t *body,
                   size_t len) {
    ava1_qitem_t *it;
    int rc = 0;
    if (c->broken) return AVA1_E_IO;
    if (len > AVA1_Q_BYTES) {
        /* One frame alone over the bound: the connection cannot carry it. */
        c->broken = 1;
        shutdown(c->fd, SHUT_RDWR);
        return AVA1_E_BUSY;
    }
    it = malloc(sizeof *it + len);
    if (!it) return AVA1_E_IO;
    it->next = NULL;
    it->type = type;
    it->flags = flags;
    it->channel = channel;
    it->len = len;
    if (len) memcpy((uint8_t *)(it + 1), body, len);
    pthread_mutex_lock(&c->qmu);
    if (c->q_closed) {
        rc = AVA1_E_CLOSED;
    } else if (c->q_n >= AVA1_Q_ENTRIES || c->q_bytes + len > AVA1_Q_BYTES) {
        /* A peer that lets the queue fill is not draining: the connection is over. */
        rc = AVA1_E_BUSY;
        c->broken = 1;
        shutdown(c->fd, SHUT_RDWR);
    } else {
        if (!c->q_started) {
            pthread_attr_t attr;
            (void)pthread_attr_init(&attr);
            (void)pthread_attr_setstacksize(&attr, 64u * 1024u);
            (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
            if (pthread_create(&c->q_thread, &attr, q_writer, c) != 0) rc = AVA1_E_IO;
            else c->q_started = 1;
            pthread_attr_destroy(&attr);
        }
        if (rc == 0) {
            if (c->q_tail) c->q_tail->next = it;
            else c->q_head = it;
            c->q_tail = it;
            c->q_n++;
            c->q_bytes += len;
            pthread_cond_signal(&c->qcv);
        }
    }
    pthread_mutex_unlock(&c->qmu);
    if (rc != 0) free(it);
    return rc;
}

void ava1_conn_drain(ava1_conn_t *c, uint32_t ms) {
    uint64_t end = ava1_now_ms() + ms;
    for (;;) {
        int idle;
        pthread_mutex_lock(&c->qmu);
        idle = !c->q_started || (!c->q_head && !c->q_busy);
        pthread_mutex_unlock(&c->qmu);
        if (idle || c->broken || ava1_now_ms() >= end) return;
        ava1_platform_sleep_ms(2);
    }
}

int ava1_conn_tune_buffers(int fd, int *rcv, int *snd) {
    int want = AVA1_LANE_SOCKBUF, got = 0, ok = 0;
    socklen_t n = sizeof got;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &want, sizeof want) == 0) ok = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &want, sizeof want) == 0) ok = 1;
    if (rcv) {
        *rcv = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &got, &n) == 0 ? got : 0;
    }
    got = 0;
    n = sizeof got;
    if (snd) {
        *snd = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &got, &n) == 0 ? got : 0;
    }
    return ok ? 0 : -1;
}
