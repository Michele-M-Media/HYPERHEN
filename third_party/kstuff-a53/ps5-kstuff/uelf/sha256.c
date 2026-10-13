#include "sha256.h"

int isal_sha256_ctx_submit_single(ISAL_SHA256_HASH_CTX* ctx, const void* buffer, uint32_t len,
                                  ISAL_HASH_CTX_FLAG flags);

static void sha256_words_to_bytes(uint8_t out[UELF_SHA256_DIGEST_SIZE],
                                  const uint32_t digest[ISAL_SHA256_DIGEST_NWORDS])
{
    for(size_t i = 0; i < ISAL_SHA256_DIGEST_NWORDS; i++)
    {
        uint32_t word = digest[i];
        out[i * 4 + 0] = (uint8_t)(word >> 24);
        out[i * 4 + 1] = (uint8_t)(word >> 16);
        out[i * 4 + 2] = (uint8_t)(word >> 8);
        out[i * 4 + 3] = (uint8_t)word;
    }
}

static int sha256_submit(struct uelf_sha256_context* ctx, const void* data, uint32_t len, uint8_t flags)
{
    return isal_sha256_ctx_submit_single(&ctx->ctx, data, len, (ISAL_HASH_CTX_FLAG)flags) ? -1 : 0;
}

void uelf_sha256_init(struct uelf_sha256_context* ctx)
{
    isal_hash_ctx_init(&ctx->ctx);
    ctx->next_flags = ISAL_HASH_FIRST;
}

int uelf_sha256_update(struct uelf_sha256_context* ctx, const void* data, size_t len)
{
    const uint8_t* p = data;

    while(len)
    {
        uint32_t chunk = len > UINT32_MAX ? UINT32_MAX : (uint32_t)len;
        if(sha256_submit(ctx, p, chunk, ctx->next_flags))
            return -1;
        ctx->next_flags = ISAL_HASH_UPDATE;
        p += chunk;
        len -= chunk;
    }
    return 0;
}

int uelf_sha256_final(struct uelf_sha256_context* ctx, uint8_t out[UELF_SHA256_DIGEST_SIZE])
{
    uint8_t flags = (ctx->next_flags == ISAL_HASH_FIRST) ? ISAL_HASH_ENTIRE : ISAL_HASH_LAST;

    if(sha256_submit(ctx, NULL, 0, flags))
        return -1;
    ctx->next_flags = ISAL_HASH_LAST;
    sha256_words_to_bytes(out, ctx->ctx.job.result_digest);
    return 0;
}

int uelf_sha256_out(const struct uelf_sha256_context* ctx, uint8_t out[UELF_SHA256_DIGEST_SIZE])
{
    struct uelf_sha256_context tmp = *ctx;
    return uelf_sha256_final(&tmp, out);
}
