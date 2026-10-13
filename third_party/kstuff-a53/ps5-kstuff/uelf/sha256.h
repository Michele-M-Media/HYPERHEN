#pragma once
#include <stddef.h>
#include <stdint.h>
#include <isa-l_crypto/sha256_mb.h>

enum {
    UELF_SHA256_DIGEST_SIZE = 32,
};

struct uelf_sha256_context
{
    ISAL_SHA256_HASH_CTX ctx __attribute__((aligned(16)));
    uint8_t next_flags;
};

void uelf_sha256_init(struct uelf_sha256_context* ctx);
int uelf_sha256_update(struct uelf_sha256_context* ctx, const void* data, size_t len);
int uelf_sha256_final(struct uelf_sha256_context* ctx, uint8_t out[UELF_SHA256_DIGEST_SIZE]);
int uelf_sha256_out(const struct uelf_sha256_context* ctx, uint8_t out[UELF_SHA256_DIGEST_SIZE]);
