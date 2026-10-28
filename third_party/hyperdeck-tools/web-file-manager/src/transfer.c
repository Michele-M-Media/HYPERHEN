#include "transfer.h"
#include "filemgr_internal.h"
#include "vfs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define TRANSFER_READ_SLOTS 3

struct transfer_reader {
  int fd, stop, done, error, started;
  struct file_task *task;
  uint64_t start, size, position;
  struct { char *data; size_t size; int ready; } slots[TRANSFER_READ_SLOTS];
  size_t used;
  int head, tail, slot_count;
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t changed;
};

static ssize_t
read_at(int fd, void *data, size_t size, uint64_t offset) {
  ssize_t n;
  do { n = pread(fd, data, size, (off_t)offset); } while(n < 0 && errno == EINTR);
  return n;
}

static void *
transfer_reader_worker(void *arg) {
  transfer_reader_t *p = arg;
  uint64_t read_size = 0;
  pthread_mutex_lock(&p->lock);
  while(read_size < p->size && !p->stop) {
    while(p->slots[p->tail].ready && !p->stop)
      pthread_cond_wait(&p->changed, &p->lock);
    if(p->stop) break;
    if(p->task && task_cancel_requested(p->task)) { p->error = ECANCELED; break; }
    size_t want = p->size - read_size < TRANSFER_READ_BUFFER_SIZE ?
      (size_t)(p->size - read_size) : TRANSFER_READ_BUFFER_SIZE;
    int slot = p->tail;
    pthread_mutex_unlock(&p->lock);
    ssize_t n = read_at(p->fd, p->slots[slot].data, want, p->start + read_size);
    int error = n < 0 ? errno : EIO;
    pthread_mutex_lock(&p->lock);
    if(n <= 0) { p->error = error; break; }
    read_size += (size_t)n;
    p->slots[slot].size = (size_t)n;
    p->slots[slot].ready = 1;
    p->tail = (slot + 1) % p->slot_count;
    pthread_cond_broadcast(&p->changed);
  }
  p->done = 1;
  pthread_cond_broadcast(&p->changed);
  pthread_mutex_unlock(&p->lock);
  return NULL;
}

static void
transfer_reader_stop(transfer_reader_t *p) {
  if(!p->started) return;
  pthread_mutex_lock(&p->lock);
  p->stop = 1;
  pthread_cond_broadcast(&p->changed);
  pthread_mutex_unlock(&p->lock);
  pthread_join(p->thread, NULL);
  p->started = 0;
}

void
transfer_reader_close(transfer_reader_t *p) {
  if(!p) return;
  int error = errno;
  transfer_reader_stop(p);
  for(int i = 0; i < TRANSFER_READ_SLOTS; i++) free(p->slots[i].data);
  pthread_cond_destroy(&p->changed);
  pthread_mutex_destroy(&p->lock);
  free(p);
  errno = error;
}

static transfer_reader_t *
transfer_reader_create(int fd, uint64_t start, uint64_t size, file_task_t *task) {
  transfer_reader_t *p = calloc(1, sizeof(*p));
  if(!p) return NULL;
  p->fd = fd; p->task = task; p->start = start; p->size = size; p->position = start;
  p->slot_count = size / TRANSFER_READ_BUFFER_SIZE >= TRANSFER_READ_SLOTS ? TRANSFER_READ_SLOTS :
    (int)(size / TRANSFER_READ_BUFFER_SIZE + (size % TRANSFER_READ_BUFFER_SIZE != 0));
  if(pthread_mutex_init(&p->lock, NULL)) { free(p); return NULL; }
  if(pthread_cond_init(&p->changed, NULL)) {
    pthread_mutex_destroy(&p->lock); free(p); return NULL;
  }
  for(int i = 0; i < p->slot_count; i++) {
    if(posix_memalign((void **)&p->slots[i].data, 4096, TRANSFER_READ_BUFFER_SIZE)) {
      transfer_reader_close(p); return NULL;
    }
  }
  if(pthread_create(&p->thread, NULL, transfer_reader_worker, p)) {
    transfer_reader_close(p); return NULL;
  }
  p->started = 1;
#if WFM_DEBUG
  fprintf(stderr, "Transfer prefetch fd=%d size=%llu buffers=%d x %d bytes\n",
          fd, (unsigned long long)size, p->slot_count, TRANSFER_READ_BUFFER_SIZE);
#endif
  return p;
}

transfer_reader_t *
transfer_reader_start(int fd, uint64_t start, uint64_t size, file_task_t *task) {
  return transfer_use_pipeline(size) ? transfer_reader_create(fd, start, size, task) : NULL;
}

transfer_reader_t *
transfer_http_reader_start(int fd, uint64_t start, uint64_t size, uint64_t file_size, file_task_t *task) {
  if(!wfm_fd_remote(fd)) return NULL;
  wfm_set_transfer_size(fd, file_size);
  /* A single buffer cannot overlap reads; avoid a worker for header probes. */
  return transfer_use_pipeline(file_size) && size > TRANSFER_READ_BUFFER_SIZE ?
    transfer_reader_create(fd, start, size, task) : NULL;
}

ssize_t
transfer_reader_peek(transfer_reader_t *p, const char **data) {
  pthread_mutex_lock(&p->lock);
  while(!p->slots[p->head].ready && !p->done)
    pthread_cond_wait(&p->changed, &p->lock);
  ssize_t n = 0;
  if(p->task && task_cancel_requested(p->task)) { errno = ECANCELED; n = -1; }
  else if(p->slots[p->head].ready) {
    *data = p->slots[p->head].data + p->used;
    n = (ssize_t)(p->slots[p->head].size - p->used);
  } else if(p->error) { errno = p->error; n = -1; }
  pthread_mutex_unlock(&p->lock);
  return n;
}

void
transfer_reader_advance(transfer_reader_t *p, size_t size) {
  pthread_mutex_lock(&p->lock);
  p->used += size; p->position += size;
  if(p->used == p->slots[p->head].size) {
    p->slots[p->head].ready = 0;
    p->head = (p->head + 1) % p->slot_count;
    p->used = 0;
    pthread_cond_broadcast(&p->changed);
  }
  pthread_mutex_unlock(&p->lock);
}

ssize_t
transfer_read_at(transfer_reader_t *p, int fd, void *data, size_t size, uint64_t offset) {
  if(!size) return 0;
  /* A callback can seek/retry. Discard prefetch before using its new position. */
  if(p && p->started && offset != p->position) transfer_reader_stop(p);
  if(!p || !p->started) return read_at(fd, data, size, offset);
  const char *buffer;
  ssize_t n = transfer_reader_peek(p, &buffer);
  if(n <= 0) return n;
  if((size_t)n > size) n = (ssize_t)size;
  memcpy(data, buffer, (size_t)n);
  transfer_reader_advance(p, (size_t)n);
  return n;
}

int transfer_write_all(file_task_t *task, int fd, const char *target,
                       const void *data, size_t size,
                       unsigned long long *written) {
  const char *bytes = data;
  size_t offset = 0;
  while(offset < size) {
    if(task && task_cancel_requested(task)) { errno = ECANCELED; return -1; }
    ssize_t n = write(fd, bytes + offset, size - offset);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) { if(!n) errno = EIO; return -1; }
    offset += (size_t)n;
    if(written) *written += (unsigned long long)n;
    if(task) task_update(task, TASK_RUNNING, target, (unsigned long long)n, NULL);
  }
  return 0;
}

void transfer_socket_option(int fd, int level, int option, int value,
                           const char *label, const char *name) {
  int saved_errno = errno;
  if(setsockopt(fd, level, option, &value, sizeof(value)) < 0) {
#if WFM_DEBUG
    fprintf(stderr, "%s fd=%d %s requested=%d failed: %s\n", label, fd, name, value, strerror(errno));
#else
    (void)label; (void)name;
#endif
  }
#if WFM_DEBUG
  if(level == SOL_SOCKET && (option == SO_RCVBUF || option == SO_SNDBUF)) {
    socklen_t size = sizeof(value);
    if(!getsockopt(fd, level, option, &value, &size))
      fprintf(stderr, "%s fd=%d %s actual=%d bytes\n", label, fd, name, value);
    else fprintf(stderr, "%s fd=%d get %s failed: %s\n", label, fd, name, strerror(errno));
  }
#endif
  errno = saved_errno;
}
