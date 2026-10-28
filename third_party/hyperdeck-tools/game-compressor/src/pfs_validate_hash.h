/*
 * Game Compressor - PFSC validation hash sidecars.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define PFS_VHASH_HASH_SIZE 32U
#define PFS_VHASH_BLOCK_SIZE 65536ULL
#define PFS_VHASH_HEADER_SIZE 4096ULL

typedef enum pfs_vhash_mode {
  PFS_VHASH_MODE_MISSING = 0,
  PFS_VHASH_MODE_INVALID,
  PFS_VHASH_MODE_STALE,
  PFS_VHASH_MODE_USED,
} pfs_vhash_mode_t;

typedef struct pfs_vhash_writer {
  int fd;
  char path[1024];
  uint64_t block_count;
} pfs_vhash_writer_t;

typedef struct pfs_vhash_reader {
  int fd;
  char path[1024];
  uint64_t block_count;
} pfs_vhash_reader_t;

const char *pfs_vhash_mode_name(pfs_vhash_mode_t mode);
int pfs_vhash_sidecar_path(const char *image_path, char *out, size_t out_size);

void pfs_sha256(const void *data, size_t size,
                unsigned char out[PFS_VHASH_HASH_SIZE]);

int pfs_vhash_writer_open(pfs_vhash_writer_t *w, const char *path,
                          uint64_t logical_size, uint64_t nested_size,
                          uint64_t block_count, const char *nested_name,
                          int nested_type, char *err, size_t err_size);
int pfs_vhash_writer_write_hash(pfs_vhash_writer_t *w, uint64_t index,
                                const unsigned char hash[PFS_VHASH_HASH_SIZE],
                                char *err, size_t err_size);
int pfs_vhash_writer_close(pfs_vhash_writer_t *w, char *err, size_t err_size);
void pfs_vhash_writer_abort(pfs_vhash_writer_t *w);

int pfs_vhash_reader_open_for_image(const char *image_path,
                                    uint64_t logical_size,
                                    uint64_t nested_size,
                                    uint64_t block_count,
                                    const char *nested_name,
                                    int nested_type,
                                    pfs_vhash_reader_t *r,
                                    pfs_vhash_mode_t *mode_out,
                                    char *err, size_t err_size);
int pfs_vhash_reader_read_hash(const pfs_vhash_reader_t *r, uint64_t index,
                               unsigned char hash[PFS_VHASH_HASH_SIZE],
                               char *err, size_t err_size);
int pfs_vhash_reader_read_hashes(const pfs_vhash_reader_t *r, uint64_t index,
                                 uint64_t count, unsigned char *hashes,
                                 char *err, size_t err_size);
void pfs_vhash_reader_close(pfs_vhash_reader_t *r);
