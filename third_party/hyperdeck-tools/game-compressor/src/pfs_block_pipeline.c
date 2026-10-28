/*
 * File Explorer - reusable fixed-block worker buffers.
 */

#include <stdlib.h>
#include <string.h>

#include "pfs_block_pipeline.h"

int
pfs_block_slots_alloc(pfs_block_slot_t **slots_out, int count,
                      size_t input_size, size_t output_size) {
  if(!slots_out || count <= 0 || output_size == 0) return -1;
  pfs_block_slot_t *slots = calloc((size_t)count, sizeof(*slots));
  if(!slots) return -1;

  for(int i = 0; i < count; i++) {
    if(input_size > 0) {
      slots[i].input = malloc(input_size);
      if(!slots[i].input) {
        pfs_block_slots_free(slots, count);
        return -1;
      }
    }
    slots[i].output = malloc(output_size);
    if(!slots[i].output) {
      pfs_block_slots_free(slots, count);
      return -1;
    }
  }

  *slots_out = slots;
  return 0;
}

void
pfs_block_slots_free(pfs_block_slot_t *slots, int count) {
  if(!slots) return;
  for(int i = 0; i < count; i++) {
    free(slots[i].input);
    free(slots[i].output);
  }
  free(slots);
}

void
pfs_stream_buffer_init(pfs_stream_buffer_t *b, size_t target_size,
                       size_t min_size) {
  memset(b, 0, sizeof(*b));
  size_t cap = target_size;
  while(cap >= min_size && cap > 0) {
    b->data = malloc(cap);
    if(b->data) {
      b->cap = cap;
      return;
    }
    cap /= 2;
  }
}

int
pfs_stream_buffer_flush(pfs_stream_buffer_t *b, int fd,
                        pfs_stream_write_fn write_fn) {
  if(!b || b->len == 0) return 0;
  if(!write_fn || write_fn(fd, b->data, b->len) != 0) return -1;
  b->len = 0;
  return 0;
}

int
pfs_stream_buffer_write(pfs_stream_buffer_t *b, int fd,
                        pfs_stream_write_fn write_fn,
                        const void *data, size_t size) {
  if(size == 0) return 0;
  if(!b || !b->data || b->cap == 0) {
    return write_fn && write_fn(fd, data, size) == 0 ? 0 : -1;
  }
  if(size > b->cap) {
    if(pfs_stream_buffer_flush(b, fd, write_fn) != 0) return -1;
    return write_fn && write_fn(fd, data, size) == 0 ? 0 : -1;
  }
  if(b->len + size > b->cap) {
    if(pfs_stream_buffer_flush(b, fd, write_fn) != 0) return -1;
  }
  memcpy(b->data + b->len, data, size);
  b->len += size;
  return 0;
}

unsigned char *
pfs_stream_buffer_reserve(pfs_stream_buffer_t *b, int fd,
                          pfs_stream_write_fn write_fn, size_t size) {
  if(size == 0 || !b || !b->data || b->cap == 0 || size > b->cap) {
    return NULL;
  }
  if(b->len + size > b->cap) {
    if(pfs_stream_buffer_flush(b, fd, write_fn) != 0) return NULL;
  }
  return b->data + b->len;
}

void
pfs_stream_buffer_commit(pfs_stream_buffer_t *b, size_t size) {
  if(!b || size > b->cap - b->len) return;
  b->len += size;
}

void
pfs_stream_buffer_free(pfs_stream_buffer_t *b) {
  if(!b) return;
  free(b->data);
  memset(b, 0, sizeof(*b));
}
