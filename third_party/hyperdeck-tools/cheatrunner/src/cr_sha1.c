#include <stdio.h>
#include <string.h>

#include "cr_sha1.h"

typedef struct {
  uint32_t h[5];
  uint64_t bits;
  uint8_t  buf[64];
  size_t   fill;
} sha1_t;

static uint32_t
rol(uint32_t v, int n) {
  return (v << n) | (v >> (32 - n));
}

static void
sha1_block(sha1_t *c, const uint8_t *p) {
  uint32_t w[80], a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
  }
  for (int i = 16; i < 80; i++) {
    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20)      { f = (b & cc) | (~b & d);          k = 0x5A827999; }
    else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
    else             { f = b ^ cc ^ d;                   k = 0xCA62C1D6; }
    uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = rol(b, 30); b = a; a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void
sha1_update(sha1_t *c, const uint8_t *p, size_t n) {
  c->bits += (uint64_t)n * 8;
  while (n) {
    size_t take = 64 - c->fill < n ? 64 - c->fill : n;
    memcpy(c->buf + c->fill, p, take);
    c->fill += take; p += take; n -= take;
    if (c->fill == 64) {
      sha1_block(c, c->buf);
      c->fill = 0;
    }
  }
}

void
cr_git_blob_sha1(const uint8_t *data, size_t len, char out[41]) {
  sha1_t c = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, 0, { 0 }, 0 };
  char hdr[32];
  int hl = snprintf(hdr, sizeof(hdr), "blob %zu", len) + 1;
  sha1_update(&c, (const uint8_t *)hdr, (size_t)hl);
  sha1_update(&c, data, len);

  uint64_t bits = c.bits;
  uint8_t pad = 0x80;
  sha1_update(&c, &pad, 1);
  uint8_t zero = 0;
  while (c.fill != 56) {
    sha1_update(&c, &zero, 1);
  }
  uint8_t be[8];
  for (int i = 0; i < 8; i++) {
    be[i] = (uint8_t)(bits >> (56 - 8 * i));
  }
  sha1_update(&c, be, 8);
  for (int i = 0; i < 5; i++) {
    snprintf(out + i * 8, 9, "%08x", c.h[i]);
  }
}
