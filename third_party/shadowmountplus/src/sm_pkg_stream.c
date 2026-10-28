#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "sm_pkg_stream.h"

// Segment cache/read-ahead follows the approach used by ps5-pkg-manager's
// ws_stream, with a fixed bound and no file-sized maps or background threads.
#define STREAM_SLOTS (32u * 1024u * 1024u / SM_PKG_STREAM_CHUNK)
#define STREAM_READ_AHEAD 8
#define STREAM_PREFETCH_PENDING 16
typedef struct {
  sm_pkg_range_t range;
  unsigned char *data;
  bool claimed, ready, consumed;
  unsigned client;
  unsigned waiters;
  uint64_t last_use;
} stream_slot_t;
struct sm_pkg_stream {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  size_t refs;
  uint64_t total, next_id, tick;
  int status;
  int wake[2];
  stream_slot_t slots[STREAM_SLOTS];
};

static struct timespec deadline_after(int seconds) {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  time.tv_sec += seconds;
  return time;
}

sm_pkg_stream_t *sm_pkg_stream_create(uint64_t total) {
  if (!total || total > INT64_MAX) { errno = EINVAL; return NULL; }
  sm_pkg_stream_t *stream = calloc(1, sizeof(*stream));
  if (!stream) return NULL;
  int rc = pthread_mutex_init(&stream->mutex, NULL);
  if (rc) { free(stream); errno = rc; return NULL; }
  rc = pthread_cond_init(&stream->cond, NULL);
  if (rc) { pthread_mutex_destroy(&stream->mutex); free(stream); errno = rc; return NULL; }
  stream->refs = 1;
  stream->wake[0] = stream->wake[1] = -1;
  stream->total = total;
  return stream;
}

void sm_pkg_stream_retain(sm_pkg_stream_t *stream) {
  pthread_mutex_lock(&stream->mutex);
  ++stream->refs;
  pthread_mutex_unlock(&stream->mutex);
}

void sm_pkg_stream_release(sm_pkg_stream_t *stream) {
  if (!stream) return;
  pthread_mutex_lock(&stream->mutex);
  bool last = --stream->refs == 0;
  pthread_mutex_unlock(&stream->mutex);
  if (!last) return;
  for (size_t i = 0; i < STREAM_SLOTS; ++i) free(stream->slots[i].data);
  if (stream->wake[0] >= 0) close(stream->wake[0]);
  if (stream->wake[1] >= 0) close(stream->wake[1]);
  pthread_cond_destroy(&stream->cond);
  pthread_mutex_destroy(&stream->mutex);
  free(stream);
}

static void notify_stream_locked(sm_pkg_stream_t *stream) {
  pthread_cond_broadcast(&stream->cond);
  if (stream->wake[1] >= 0) {
    unsigned char byte = 1;
    ssize_t written;
    do { written = write(stream->wake[1], &byte, 1); } while (written < 0 && errno == EINTR);
    (void)written; // EAGAIN means a wake is already queued.
  }
}

int sm_pkg_stream_wakeup_fd(sm_pkg_stream_t *stream) {
  pthread_mutex_lock(&stream->mutex);
  if (stream->wake[0] < 0) {
    int fds[2];
    if (pipe(fds) == 0) {
      if (fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0 && fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0) {
        stream->wake[0] = fds[0]; stream->wake[1] = fds[1];
      } else { close(fds[0]); close(fds[1]); }
    }
  }
  int fd = stream->wake[0];
  pthread_mutex_unlock(&stream->mutex);
  return fd;
}

void sm_pkg_stream_fail(sm_pkg_stream_t *stream, int status) {
  if (!stream || status <= 0) return;
  pthread_mutex_lock(&stream->mutex);
  if (!stream->status) {
    stream->status = status;
    notify_stream_locked(stream);
  }
  pthread_mutex_unlock(&stream->mutex);
}

void sm_pkg_stream_stop(sm_pkg_stream_t *stream) {
  sm_pkg_stream_fail(stream, ECANCELED);
}

int sm_pkg_stream_status(sm_pkg_stream_t *stream) {
  pthread_mutex_lock(&stream->mutex);
  int status = stream->status;
  pthread_mutex_unlock(&stream->mutex);
  return status;
}

static stream_slot_t *find_segment_locked(sm_pkg_stream_t *stream, uint64_t offset) {
  for (size_t i = 0; i < STREAM_SLOTS; ++i)
    if (stream->slots[i].range.id && stream->slots[i].range.offset == offset)
      return &stream->slots[i];
  return NULL;
}

static stream_slot_t *request_segment_locked(sm_pkg_stream_t *stream, uint64_t offset, bool demanded) {
  stream_slot_t *slot = find_segment_locked(stream, offset);
  if (slot) return slot;
  for (size_t i = 0; i < STREAM_SLOTS; ++i) {
    stream_slot_t *candidate = &stream->slots[i];
    if (!candidate->range.id) { slot = candidate; break; }
    // Never evict the header, an in-flight upload, or a waited-on segment.
    if (!candidate->ready || candidate->waiters || !candidate->range.offset) continue;
    // Speculation must not discard a block another reader has yet to consume.
    if (!demanded && !candidate->consumed) continue;
    if (!slot || candidate->last_use < slot->last_use) slot = candidate;
  }
  if (!slot) { errno = EAGAIN; return NULL; }
  size_t length = stream->total - offset < SM_PKG_STREAM_CHUNK
      ? (size_t)(stream->total - offset) : SM_PKG_STREAM_CHUNK;
  unsigned char *data = realloc(slot->data, length);
  if (!data) { errno = ENOMEM; return NULL; }
  *slot = (stream_slot_t){.data = data, .last_use = ++stream->tick,
      .range = {.id = ++stream->next_id, .offset = offset, .size = length}};
  notify_stream_locked(stream);
  return slot;
}

static void prefetch_segments_locked(sm_pkg_stream_t *stream, uint64_t offset) {
  unsigned pending = 0;
  for (size_t i = 0; i < STREAM_SLOTS; ++i)
    if (stream->slots[i].range.id && !stream->slots[i].ready) ++pending;
  for (unsigned i = 1; i <= STREAM_READ_AHEAD; ++i) {
    uint64_t next = offset + (uint64_t)i * SM_PKG_STREAM_CHUNK;
    if (next >= stream->total) break;
    if (find_segment_locked(stream, next)) continue;
    if (pending >= STREAM_PREFETCH_PENDING || !request_segment_locked(stream, next, false)) break;
    ++pending;
  }
}

int sm_pkg_stream_read(void *ctx, uint64_t offset, void *buffer, size_t size) {
  sm_pkg_stream_t *stream = ctx;
  if (offset > stream->total || size > stream->total - offset) return EINVAL;
  unsigned char *out = buffer;
  bool bulk = size >= 64u * 1024u;
  while (size) {
    uint64_t start = offset - offset % SM_PKG_STREAM_CHUNK;
    size_t within = (size_t)(offset - start);
    size_t length = SM_PKG_STREAM_CHUNK - within;
    if (length > size) length = size;
    struct timespec deadline = deadline_after(60);
    pthread_mutex_lock(&stream->mutex);
    stream_slot_t *slot = NULL;
    int status = 0;
    while (!stream->status && !slot && !status) {
      slot = request_segment_locked(stream, start, true);
      if (!slot) status = errno == ENOMEM ? ENOMEM :
          pthread_cond_timedwait(&stream->cond, &stream->mutex, &deadline);
    }
    if (!status && !stream->status) {
      ++slot->waiters;
      if (bulk) prefetch_segments_locked(stream, start);
      while (!stream->status && !slot->ready && !status)
        status = pthread_cond_timedwait(&stream->cond, &stream->mutex, &deadline);
      if (!status && !stream->status) {
        memcpy(out, slot->data + within, length);
        slot->consumed = true;
        slot->last_use = ++stream->tick;
      }
      --slot->waiters;
      pthread_cond_broadcast(&stream->cond);
    }
    if (stream->status) status = stream->status;
    pthread_mutex_unlock(&stream->mutex);
    if (status) { sm_pkg_stream_fail(stream, status); return status; }
    offset += length; out += length; size -= length;
  }
  return 0;
}

static stream_slot_t *next_request_locked(sm_pkg_stream_t *stream) {
  // Cache slots are recycled in LRU order. Their array indices no longer
  // describe file order after eviction, so choose the oldest queued request
  // within each priority class instead of jumping between recycled slots.
  for (unsigned pass = 0; pass < 2; ++pass) {
    stream_slot_t *next = NULL;
    for (size_t i = 0; i < STREAM_SLOTS; ++i) {
      stream_slot_t *slot = &stream->slots[i];
      if (!slot->range.id || slot->claimed || slot->ready || (!pass && !slot->waiters)) continue;
      if (!next || slot->range.id < next->range.id) next = slot;
    }
    if (next) return next;
  }
  return NULL;
}

int sm_pkg_stream_request(sm_pkg_stream_t *stream, sm_pkg_range_t *range) {
  struct timespec deadline = deadline_after(8);
  pthread_mutex_lock(&stream->mutex);
  int status = 0;
  for (;;) {
    if (stream->status) { status = stream->status; break; }
    stream_slot_t *slot = next_request_locked(stream);
    if (slot) {
      slot->claimed = true;
      *range = slot->range;
      break;
    }
    status = pthread_cond_timedwait(&stream->cond, &stream->mutex, &deadline);
    if (status) { if (status == ETIMEDOUT) status = EAGAIN; break; }
  }
  pthread_mutex_unlock(&stream->mutex);
  return status;
}

int sm_pkg_stream_try_request(sm_pkg_stream_t *stream, unsigned client, sm_pkg_range_t *range) {
  if (!stream || !range || client > SM_PKG_STREAM_CLIENTS) return EINVAL;
  memset(range, 0, sizeof(*range));
  pthread_mutex_lock(&stream->mutex);
  int status = stream->status ? stream->status : EAGAIN;
  if (!stream->status && client) {
    for (size_t i = 0; i < STREAM_SLOTS; ++i) {
      stream_slot_t *slot = &stream->slots[i];
      if (!slot->range.id || slot->ready || !slot->claimed || slot->client != client) continue;
      *range = slot->range;
      status = 0;
      goto done;
    }
  }
  // Claim demanded segments before speculative read-ahead.
  if (!stream->status) {
    stream_slot_t *slot = next_request_locked(stream);
    if (slot) {
      slot->claimed = true;
      slot->client = client;
      *range = slot->range;
      status = 0;
    }
  }
done:
  pthread_mutex_unlock(&stream->mutex);
  return status;
}

int sm_pkg_stream_reply(sm_pkg_stream_t *stream, uint64_t id,
                        const void *data, size_t size) {
  pthread_mutex_lock(&stream->mutex);
  int status = stream->status ? stream->status : ENOENT;
  if (!stream->status) for (size_t i = 0; i < STREAM_SLOTS; ++i) {
    stream_slot_t *slot = &stream->slots[i];
    if (slot->range.id != id || !id) continue;
    if (size != slot->range.size) status = EINVAL;
    else if (slot->ready) status = memcmp(slot->data, data, size) ? EINVAL : 0;
    else if (!slot->claimed) status = EINVAL;
    else {
      memcpy(slot->data, data, size);
      slot->ready = true;
      slot->last_use = ++stream->tick;
      slot->client = 0;
      pthread_cond_broadcast(&stream->cond);
      status = 0;
    }
    break;
  }
  pthread_mutex_unlock(&stream->mutex);
  return status;
}
