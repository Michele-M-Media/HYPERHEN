/*
 * File Explorer - reusable fixed-block worker buffers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum pfs_block_slot_state {
  PFS_BLOCK_SLOT_FREE = 0,
  PFS_BLOCK_SLOT_FILLING,
  PFS_BLOCK_SLOT_READY,
  PFS_BLOCK_SLOT_BUSY,
  PFS_BLOCK_SLOT_DONE,
} pfs_block_slot_state_t;

typedef struct pfs_block_slot {
  uint64_t index;
  size_t input_len;
  size_t output_len;
  int flag;
  unsigned char *input;
  unsigned char *output;
  pfs_block_slot_state_t state;
} pfs_block_slot_t;

typedef int (*pfs_stream_write_fn)(int fd, const void *data, size_t size);

typedef struct pfs_stream_buffer {
  unsigned char *data;
  size_t len;
  size_t cap;
} pfs_stream_buffer_t;

int pfs_block_slots_alloc(pfs_block_slot_t **slots_out, int count,
                          size_t input_size, size_t output_size);
void pfs_block_slots_free(pfs_block_slot_t *slots, int count);

void pfs_stream_buffer_init(pfs_stream_buffer_t *b, size_t target_size,
                            size_t min_size);
int pfs_stream_buffer_write(pfs_stream_buffer_t *b, int fd,
                            pfs_stream_write_fn write_fn,
                            const void *data, size_t size);
unsigned char *pfs_stream_buffer_reserve(pfs_stream_buffer_t *b, int fd,
                                         pfs_stream_write_fn write_fn,
                                         size_t size);
void pfs_stream_buffer_commit(pfs_stream_buffer_t *b, size_t size);
int pfs_stream_buffer_flush(pfs_stream_buffer_t *b, int fd,
                            pfs_stream_write_fn write_fn);
void pfs_stream_buffer_free(pfs_stream_buffer_t *b);
