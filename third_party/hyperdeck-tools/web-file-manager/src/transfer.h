#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct file_task;

#define TRANSFER_SOCKET_BUFFER_SIZE (512 * 1024)
#define TRANSFER_LARGE_FILE_THRESHOLD (256ULL * 1024 * 1024)
#define TRANSFER_READ_BUFFER_SIZE (8 * 1024 * 1024)
#define TRANSFER_HTTP_BUFFER_SIZE (2 * 1024 * 1024)

/* All size-based transfer optimizations use this one policy. */
static inline int transfer_use_pipeline(uint64_t size) {
  return size >= TRANSFER_LARGE_FILE_THRESHOLD;
}

typedef struct transfer_reader transfer_reader_t;
/* Borrows fd. Small files and allocation/thread failures use direct reads. */
transfer_reader_t *transfer_reader_start(int fd, uint64_t start, uint64_t size,
                                         struct file_task *task);
/* HTTP copies prefetch data into MHD's buffer; native files read there directly. */
transfer_reader_t *transfer_http_reader_start(int fd, uint64_t start, uint64_t size,
                                              uint64_t file_size, struct file_task *task);
ssize_t transfer_reader_peek(transfer_reader_t *reader, const char **data);
void transfer_reader_advance(transfer_reader_t *reader, size_t size);
ssize_t transfer_read_at(transfer_reader_t *reader, int fd, void *data,
                         size_t size, uint64_t offset);
void transfer_reader_close(transfer_reader_t *reader);

/* Shared disk/SMB write loop for copies, HTTP uploads and text saves. */
int transfer_write_all(struct file_task *task, int fd, const char *target,
                       const void *data, size_t size,
                       unsigned long long *written);
void transfer_socket_option(int fd, int level, int option, int value,
                           const char *label, const char *name);
