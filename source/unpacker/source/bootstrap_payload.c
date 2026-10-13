/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_payload.h"

#include <onion/log.h>

#include <stdint.h>
#include <stdlib.h>

#include "LzmaLib.h"

#define LZMA_CLI_HEADER_SIZE 13

#ifndef HYPERHEN_BOOTSTRAPPER_LZMA
#define HYPERHEN_BOOTSTRAPPER_LZMA "../../bin/bootstrapper.elf.lzma"
#endif
#ifndef HYPERHEN_BOOTSTRAPPER_SIZE
#define HYPERHEN_BOOTSTRAPPER_SIZE "../../bin/bootstrapper.elf.lzma.size"
#endif
#ifndef HYPERHEN_BOOTSTRAPPER_SHA1
#define HYPERHEN_BOOTSTRAPPER_SHA1 "../../bin/bootstrapper.elf.sha1"
#endif

__asm__(".intel_syntax noprefix\n"
        ".section .data\n"
        ".global hyperhen_compressed\n"
        ".type   hyperhen_compressed, @object\n"
        ".align  16\n"
        "hyperhen_compressed:\n"
        ".incbin \"" HYPERHEN_BOOTSTRAPPER_LZMA "\"\n"
        "hyperhen_compressed_end:\n"
        ".global hyperhen_compressed_size\n"
        ".type  hyperhen_compressed_size, @object\n"
        ".align  4\n"
        "hyperhen_compressed_size:\n"
        ".int    hyperhen_compressed_end - hyperhen_compressed\n"
        ".global hyperhen_decompressed_size\n"
        ".type   hyperhen_decompressed_size, @object\n"
        ".align  16\n"
        "hyperhen_decompressed_size:\n"
        ".incbin \"" HYPERHEN_BOOTSTRAPPER_SIZE "\"\n"
        ".byte 0\n"
        ".global hyperhen_sha1_hex\n"
        ".type   hyperhen_sha1_hex, @object\n"
        ".align  16\n"
        "hyperhen_sha1_hex:\n"
        ".incbin \"" HYPERHEN_BOOTSTRAPPER_SHA1 "\"\n"
        ".byte 0\n");

extern uint32_t hyperhen_compressed_size;
extern uint8_t hyperhen_compressed[];
extern uint8_t hyperhen_decompressed_size[];
extern uint8_t hyperhen_sha1_hex[];

size_t bootstrap_payload_expected_size(void) {
  char *end = NULL;
  const unsigned long n =
      strtoul((const char *)hyperhen_decompressed_size, &end, 10);
  if (end == (char *)hyperhen_decompressed_size || n == 0)
    return 0;
  return (size_t)n;
}

bool bootstrap_payload_expected_sha1(uint8_t digest[SHA1_DIGEST_SIZE]) {
  return sha1_parse_hex((const char *)hyperhen_sha1_hex, digest);
}

bootstrap_payload_status bootstrap_payload_decompress(uint8_t **out,
                                                      size_t *out_size) {
  if (!out || !out_size)
    return BOOTSTRAP_PAYLOAD_INVALID;
  *out = NULL;
  *out_size = 0;

  if (hyperhen_compressed_size <= LZMA_CLI_HEADER_SIZE)
    return BOOTSTRAP_PAYLOAD_INVALID;

  const size_t expected = bootstrap_payload_expected_size();
  if (expected == 0)
    return BOOTSTRAP_PAYLOAD_INVALID;

  uint8_t *buf = (uint8_t *)malloc(expected);
  if (!buf)
    return BOOTSTRAP_PAYLOAD_NOMEM;

  size_t dest_len = expected;
  size_t src_len = hyperhen_compressed_size;
  const int res =
      LzmaUncompress(buf, &dest_len, hyperhen_compressed + LZMA_CLI_HEADER_SIZE,
                     &src_len, hyperhen_compressed, LZMA_PROPS_SIZE);
  if (res != 0 || dest_len == 0) {
    LOG_ERROR("LzmaUncompress failed: %d", res);
    free(buf);
    return BOOTSTRAP_PAYLOAD_LZMA;
  }

  *out = buf;
  *out_size = dest_len;
  return BOOTSTRAP_PAYLOAD_OK;
}
