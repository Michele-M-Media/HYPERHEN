/*
 * Game Compressor - PFSC validation hash sidecars.
 */

#include "pfs_validate_hash.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PFS_VHASH_MAGIC "PFSCVHS1"
#define PFS_VHASH_VERSION 1U
#define PFS_VHASH_ALGO_SHA256 1U
#define PFS_VHASH_NESTED_NAME_OFFSET 128U
#define PFS_VHASH_NESTED_NAME_SIZE 256U

static void
set_err(char *err, size_t err_size, const char *fmt, ...) {
  if(!err || err_size == 0 || err[0]) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, err_size, fmt, ap);
  va_end(ap);
}

static void
le32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

static void
le64(unsigned char *p, uint64_t v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
  p[4] = (unsigned char)(v >> 32);
  p[5] = (unsigned char)(v >> 40);
  p[6] = (unsigned char)(v >> 48);
  p[7] = (unsigned char)(v >> 56);
}

static uint32_t
rd32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
rd64(const unsigned char *p) {
  return (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
         ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
         ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static int
read_exact_at(int fd, void *data, size_t size, uint64_t offset) {
  unsigned char *p = data;
  while(size > 0) {
    ssize_t n = pread(fd, p, size, (off_t)offset);
    if(n < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(n == 0) {
      errno = EIO;
      return -1;
    }
    p += n;
    size -= (size_t)n;
    offset += (uint64_t)n;
  }
  return 0;
}

static int
write_exact_at(int fd, const void *data, size_t size, uint64_t offset) {
  const unsigned char *p = data;
  while(size > 0) {
    ssize_t n = pwrite(fd, p, size, (off_t)offset);
    if(n < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(n == 0) {
      errno = EIO;
      return -1;
    }
    p += n;
    size -= (size_t)n;
    offset += (uint64_t)n;
  }
  return 0;
}

static uint32_t
ror32(uint32_t v, unsigned bits) {
  return (v >> bits) | (v << (32U - bits));
}

typedef struct sha256_ctx {
  uint32_t h[8];
  uint64_t len;
  unsigned char buf[64];
  size_t buf_len;
} sha256_ctx_t;

static const uint32_t k256[64] = {
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
  0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
  0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
  0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
  0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
  0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
  0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
  0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
  0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void
sha256_init(sha256_ctx_t *ctx) {
  ctx->h[0] = 0x6a09e667U;
  ctx->h[1] = 0xbb67ae85U;
  ctx->h[2] = 0x3c6ef372U;
  ctx->h[3] = 0xa54ff53aU;
  ctx->h[4] = 0x510e527fU;
  ctx->h[5] = 0x9b05688cU;
  ctx->h[6] = 0x1f83d9abU;
  ctx->h[7] = 0x5be0cd19U;
  ctx->len = 0;
  ctx->buf_len = 0;
}

static void
sha256_block(sha256_ctx_t *ctx, const unsigned char block[64]) {
  uint32_t w[64];
  for(unsigned i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[i * 4] << 24) |
           ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) |
           (uint32_t)block[i * 4 + 3];
  }
  for(unsigned i = 16; i < 64; i++) {
    uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^
                  (w[i - 15] >> 3);
    uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^
                  (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
  uint32_t e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];
  for(unsigned i = 0; i < 64; i++) {
    uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + k256[i] + w[i];
    uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx->h[0] += a;
  ctx->h[1] += b;
  ctx->h[2] += c;
  ctx->h[3] += d;
  ctx->h[4] += e;
  ctx->h[5] += f;
  ctx->h[6] += g;
  ctx->h[7] += h;
}

static void
sha256_update(sha256_ctx_t *ctx, const void *data, size_t size) {
  const unsigned char *p = data;
  ctx->len += (uint64_t)size;
  while(size > 0) {
    size_t n = sizeof(ctx->buf) - ctx->buf_len;
    if(n > size) n = size;
    memcpy(ctx->buf + ctx->buf_len, p, n);
    ctx->buf_len += n;
    p += n;
    size -= n;
    if(ctx->buf_len == sizeof(ctx->buf)) {
      sha256_block(ctx, ctx->buf);
      ctx->buf_len = 0;
    }
  }
}

static void
sha256_final(sha256_ctx_t *ctx, unsigned char out[PFS_VHASH_HASH_SIZE]) {
  uint64_t bit_len = ctx->len * 8ULL;
  ctx->buf[ctx->buf_len++] = 0x80U;
  if(ctx->buf_len > 56) {
    while(ctx->buf_len < 64) ctx->buf[ctx->buf_len++] = 0;
    sha256_block(ctx, ctx->buf);
    ctx->buf_len = 0;
  }
  while(ctx->buf_len < 56) ctx->buf[ctx->buf_len++] = 0;
  for(int i = 7; i >= 0; i--) {
    ctx->buf[ctx->buf_len++] = (unsigned char)(bit_len >> (unsigned)i * 8U);
  }
  sha256_block(ctx, ctx->buf);
  for(unsigned i = 0; i < 8; i++) {
    out[i * 4] = (unsigned char)(ctx->h[i] >> 24);
    out[i * 4 + 1] = (unsigned char)(ctx->h[i] >> 16);
    out[i * 4 + 2] = (unsigned char)(ctx->h[i] >> 8);
    out[i * 4 + 3] = (unsigned char)ctx->h[i];
  }
}

void
pfs_sha256(const void *data, size_t size,
           unsigned char out[PFS_VHASH_HASH_SIZE]) {
  sha256_ctx_t ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, data, size);
  sha256_final(&ctx, out);
}

const char *
pfs_vhash_mode_name(pfs_vhash_mode_t mode) {
  if(mode == PFS_VHASH_MODE_USED) return "used";
  if(mode == PFS_VHASH_MODE_STALE) return "stale";
  if(mode == PFS_VHASH_MODE_INVALID) return "invalid";
  return "missing";
}

int
pfs_vhash_sidecar_path(const char *image_path, char *out, size_t out_size) {
  int n = snprintf(out, out_size, "%s.vhash", image_path ? image_path : "");
  return n >= 0 && (size_t)n < out_size ? 0 : -1;
}

static void
fill_header(unsigned char *hdr, uint64_t logical_size, uint64_t nested_size,
            uint64_t block_count, const char *nested_name, int nested_type) {
  memset(hdr, 0, (size_t)PFS_VHASH_HEADER_SIZE);
  memcpy(hdr, PFS_VHASH_MAGIC, 8);
  le32(hdr + 8, PFS_VHASH_VERSION);
  le32(hdr + 12, (uint32_t)PFS_VHASH_HEADER_SIZE);
  le64(hdr + 16, PFS_VHASH_BLOCK_SIZE);
  le64(hdr + 24, logical_size);
  le64(hdr + 32, nested_size);
  le64(hdr + 40, block_count);
  le32(hdr + 48, (uint32_t)nested_type);
  le32(hdr + 52, PFS_VHASH_ALGO_SHA256);
  snprintf((char *)hdr + PFS_VHASH_NESTED_NAME_OFFSET,
           PFS_VHASH_NESTED_NAME_SIZE, "%s", nested_name ? nested_name : "");
}

int
pfs_vhash_writer_open(pfs_vhash_writer_t *w, const char *path,
                      uint64_t logical_size, uint64_t nested_size,
                      uint64_t block_count, const char *nested_name,
                      int nested_type, char *err, size_t err_size) {
  if(!w || !path || !*path) {
    set_err(err, err_size, "bad validation hash path");
    errno = EINVAL;
    return -1;
  }
  memset(w, 0, sizeof(*w));
  w->fd = -1;
  snprintf(w->path, sizeof(w->path), "%s", path);
  w->block_count = block_count;
  unsigned char *hdr = calloc(1, (size_t)PFS_VHASH_HEADER_SIZE);
  if(!hdr) {
    set_err(err, err_size, "out of memory");
    errno = ENOMEM;
    return -1;
  }
  fill_header(hdr, logical_size, nested_size, block_count, nested_name,
              nested_type);
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd < 0) {
    set_err(err, err_size, "create validation hash: %s", strerror(errno));
    free(hdr);
    return -1;
  }
  if(write_exact_at(fd, hdr, (size_t)PFS_VHASH_HEADER_SIZE, 0) != 0) {
    set_err(err, err_size, "write validation hash header: %s", strerror(errno));
    close(fd);
    unlink(path);
    free(hdr);
    return -1;
  }
  free(hdr);
  w->fd = fd;
  return 0;
}

int
pfs_vhash_writer_write_hash(pfs_vhash_writer_t *w, uint64_t index,
                            const unsigned char hash[PFS_VHASH_HASH_SIZE],
                            char *err, size_t err_size) {
  if(!w || w->fd < 0 || !hash || index >= w->block_count) {
    set_err(err, err_size, "bad validation hash write");
    errno = EINVAL;
    return -1;
  }
  uint64_t off = PFS_VHASH_HEADER_SIZE + index * PFS_VHASH_HASH_SIZE;
  if(write_exact_at(w->fd, hash, PFS_VHASH_HASH_SIZE, off) != 0) {
    set_err(err, err_size, "write validation hash: %s", strerror(errno));
    return -1;
  }
  return 0;
}

int
pfs_vhash_writer_close(pfs_vhash_writer_t *w, char *err, size_t err_size) {
  if(!w || w->fd < 0) return 0;
  int fd = w->fd;
  w->fd = -1;
  if(fsync(fd) != 0) {
    set_err(err, err_size, "sync validation hash: %s", strerror(errno));
    close(fd);
    return -1;
  }
  if(close(fd) != 0) {
    set_err(err, err_size, "close validation hash: %s", strerror(errno));
    return -1;
  }
  return 0;
}

void
pfs_vhash_writer_abort(pfs_vhash_writer_t *w) {
  if(!w) return;
  if(w->fd >= 0) close(w->fd);
  if(w->path[0]) unlink(w->path);
  memset(w, 0, sizeof(*w));
  w->fd = -1;
}

static int
header_matches(const unsigned char *hdr, uint64_t logical_size,
               uint64_t nested_size, uint64_t block_count,
               const char *nested_name, int nested_type) {
  if(memcmp(hdr, PFS_VHASH_MAGIC, 8) != 0 ||
     rd32(hdr + 8) != PFS_VHASH_VERSION ||
     rd32(hdr + 12) != (uint32_t)PFS_VHASH_HEADER_SIZE ||
     rd64(hdr + 16) != PFS_VHASH_BLOCK_SIZE ||
     rd32(hdr + 52) != PFS_VHASH_ALGO_SHA256) {
    return -1;
  }
  if(rd64(hdr + 24) != logical_size ||
     rd64(hdr + 32) != nested_size ||
     rd64(hdr + 40) != block_count ||
     rd32(hdr + 48) != (uint32_t)nested_type ||
     strncmp((const char *)hdr + PFS_VHASH_NESTED_NAME_OFFSET,
             nested_name ? nested_name : "",
             PFS_VHASH_NESTED_NAME_SIZE) != 0) {
    return 0;
  }
  return 1;
}

int
pfs_vhash_reader_open_for_image(const char *image_path, uint64_t logical_size,
                                uint64_t nested_size, uint64_t block_count,
                                const char *nested_name, int nested_type,
                                pfs_vhash_reader_t *r,
                                pfs_vhash_mode_t *mode_out,
                                char *err, size_t err_size) {
  char path[1024];
  unsigned char *hdr = NULL;
  struct stat st;
  pfs_vhash_mode_t mode = PFS_VHASH_MODE_MISSING;
  if(mode_out) *mode_out = mode;
  if(!r) {
    set_err(err, err_size, "bad validation hash reader");
    errno = EINVAL;
    return -1;
  }
  memset(r, 0, sizeof(*r));
  r->fd = -1;
  if(pfs_vhash_sidecar_path(image_path, path, sizeof(path)) != 0) {
    if(mode_out) *mode_out = PFS_VHASH_MODE_INVALID;
    return 1;
  }
  int fd = open(path, O_RDONLY);
  if(fd < 0) {
    if(errno == ENOENT) {
      if(mode_out) *mode_out = PFS_VHASH_MODE_MISSING;
      return 1;
    }
    if(mode_out) *mode_out = PFS_VHASH_MODE_INVALID;
    return 1;
  }
  hdr = malloc((size_t)PFS_VHASH_HEADER_SIZE);
  if(!hdr) {
    close(fd);
    set_err(err, err_size, "out of memory");
    errno = ENOMEM;
    return -1;
  }
  if(fstat(fd, &st) != 0 || st.st_size < 0 ||
     (uint64_t)st.st_size <
         PFS_VHASH_HEADER_SIZE + block_count * PFS_VHASH_HASH_SIZE ||
     read_exact_at(fd, hdr, (size_t)PFS_VHASH_HEADER_SIZE, 0) != 0) {
    close(fd);
    free(hdr);
    if(mode_out) *mode_out = PFS_VHASH_MODE_INVALID;
    return 1;
  }
  int match = header_matches(hdr, logical_size, nested_size, block_count,
                             nested_name, nested_type);
  free(hdr);
  if(match != 1) {
    close(fd);
    if(mode_out) {
      *mode_out = match < 0 ? PFS_VHASH_MODE_INVALID : PFS_VHASH_MODE_STALE;
    }
    return 1;
  }
  r->fd = fd;
  r->block_count = block_count;
  snprintf(r->path, sizeof(r->path), "%s", path);
  if(mode_out) *mode_out = PFS_VHASH_MODE_USED;
  return 0;
}

int
pfs_vhash_reader_read_hash(const pfs_vhash_reader_t *r, uint64_t index,
                           unsigned char hash[PFS_VHASH_HASH_SIZE],
                           char *err, size_t err_size) {
  if(!r || r->fd < 0 || !hash || index >= r->block_count) {
    set_err(err, err_size, "bad validation hash read");
    errno = EINVAL;
    return -1;
  }
  uint64_t off = PFS_VHASH_HEADER_SIZE + index * PFS_VHASH_HASH_SIZE;
  if(read_exact_at(r->fd, hash, PFS_VHASH_HASH_SIZE, off) != 0) {
    set_err(err, err_size, "read validation hash: %s", strerror(errno));
    return -1;
  }
  return 0;
}

int
pfs_vhash_reader_read_hashes(const pfs_vhash_reader_t *r, uint64_t index,
                             uint64_t count, unsigned char *hashes,
                             char *err, size_t err_size) {
  if(count == 0) return 0;
  if(!r || r->fd < 0 || !hashes || index >= r->block_count ||
     count > r->block_count - index ||
     count > (uint64_t)SIZE_MAX / PFS_VHASH_HASH_SIZE) {
    set_err(err, err_size, "bad validation hash range read");
    errno = EINVAL;
    return -1;
  }
  uint64_t off = PFS_VHASH_HEADER_SIZE + index * PFS_VHASH_HASH_SIZE;
  size_t size = (size_t)(count * PFS_VHASH_HASH_SIZE);
  if(read_exact_at(r->fd, hashes, size, off) != 0) {
    set_err(err, err_size, "read validation hashes: %s", strerror(errno));
    return -1;
  }
  return 0;
}

void
pfs_vhash_reader_close(pfs_vhash_reader_t *r) {
  if(!r) return;
  if(r->fd >= 0) close(r->fd);
  memset(r, 0, sizeof(*r));
  r->fd = -1;
}
