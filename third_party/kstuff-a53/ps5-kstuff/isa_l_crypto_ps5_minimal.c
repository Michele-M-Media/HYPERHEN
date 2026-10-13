#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <isa-l_crypto/aes_cbc.h>
#include <isa-l_crypto/aes_keyexp.h>
#include <isa-l_crypto/aes_xts.h>
#include <isa-l_crypto/isal_crypto_api.h>
#include <isa-l_crypto/sha256_mb.h>

void _aes_keyexp_128_avx(const uint8_t *key, uint8_t *exp_key_enc, uint8_t *exp_key_dec);
void _aes_cbc_dec_128_avx(void *in, uint8_t *iv, uint8_t *keys, void *out, uint64_t len_bytes);
void _XTS_AES_128_enc_expanded_key_avx(uint8_t *k2, uint8_t *k1, uint8_t *initial_tweak,
                                       uint64_t len_bytes, const uint8_t *in, uint8_t *out);
void _XTS_AES_128_dec_expanded_key_avx(uint8_t *k2, uint8_t *k1, uint8_t *initial_tweak,
                                       uint64_t len_bytes, const uint8_t *in, uint8_t *out);
void _sha256_ni_x1_zen2(void *args, uint32_t blocks);

struct ps5_sha256_x1_args
{
        uint32_t digest[ISAL_SHA256_DIGEST_NWORDS][ISAL_SHA256_MAX_LANES];
        uint8_t *data_ptr[1];
} __attribute__((aligned(64)));

static void
sha256_hash_init(ISAL_SHA256_WORD_T *digest)
{
        static const ISAL_SHA256_WORD_T initial_digest[ISAL_SHA256_DIGEST_NWORDS] = {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
        };

        memcpy(digest, initial_digest, sizeof(initial_digest));
}

static void
sha256_hash_blocks_zen2(ISAL_SHA256_WORD_T *digest, const void *buffer, uint32_t blocks)
{
        struct ps5_sha256_x1_args args = {0};
        size_t i;

        if (blocks == 0)
                return;

        for (i = 0; i < ISAL_SHA256_DIGEST_NWORDS; i++)
                args.digest[i][0] = digest[i];
        args.data_ptr[0] = (uint8_t *) buffer;

        _sha256_ni_x1_zen2(&args, blocks);

        for (i = 0; i < ISAL_SHA256_DIGEST_NWORDS; i++)
                digest[i] = args.digest[i][0];
}

static void
sha256_ctx_init(ISAL_SHA256_HASH_CTX *ctx)
{
        sha256_hash_init(ctx->job.result_digest);
        ctx->total_length = 0;
        ctx->partial_block_buffer_length = 0;
        ctx->error = ISAL_HASH_CTX_ERROR_NONE;
        ctx->status = ISAL_HASH_CTX_STS_PROCESSING;
}

static void
sha256_ctx_update(ISAL_SHA256_HASH_CTX *ctx, const void *buffer, uint32_t len)
{
        const uint8_t *src = buffer;
        uint32_t remaining = len;

        ctx->total_length += len;

        if (ctx->partial_block_buffer_length != 0 || remaining < ISAL_SHA256_BLOCK_SIZE) {
                uint32_t copy_len = ISAL_SHA256_BLOCK_SIZE - ctx->partial_block_buffer_length;
                if (remaining < copy_len)
                        copy_len = remaining;

                if (copy_len != 0) {
                        memcpy(&ctx->partial_block_buffer[ctx->partial_block_buffer_length], src,
                               copy_len);
                        ctx->partial_block_buffer_length += copy_len;
                        src += copy_len;
                        remaining -= copy_len;
                }

                if (ctx->partial_block_buffer_length == ISAL_SHA256_BLOCK_SIZE) {
                        sha256_hash_blocks_zen2(ctx->job.result_digest, ctx->partial_block_buffer, 1);
                        ctx->partial_block_buffer_length = 0;
                }
        }

        if (remaining >= ISAL_SHA256_BLOCK_SIZE) {
                uint32_t blocks = remaining / ISAL_SHA256_BLOCK_SIZE;
                sha256_hash_blocks_zen2(ctx->job.result_digest, src, blocks);
                src += (uint64_t) blocks * ISAL_SHA256_BLOCK_SIZE;
                remaining -= blocks * ISAL_SHA256_BLOCK_SIZE;
        }

        if (remaining != 0) {
                memcpy(ctx->partial_block_buffer, src, remaining);
                ctx->partial_block_buffer_length = remaining;
        }

        ctx->status = ISAL_HASH_CTX_STS_IDLE;
}

static void
sha256_store_bit_length(uint8_t *dst, uint64_t bit_length)
{
        dst[0] = (uint8_t) (bit_length >> 56);
        dst[1] = (uint8_t) (bit_length >> 48);
        dst[2] = (uint8_t) (bit_length >> 40);
        dst[3] = (uint8_t) (bit_length >> 32);
        dst[4] = (uint8_t) (bit_length >> 24);
        dst[5] = (uint8_t) (bit_length >> 16);
        dst[6] = (uint8_t) (bit_length >> 8);
        dst[7] = (uint8_t) bit_length;
}

static void
sha256_ctx_final(ISAL_SHA256_HASH_CTX *ctx)
{
        uint8_t pad[ISAL_SHA256_BLOCK_SIZE * 2] = {0};
        uint32_t used = ctx->partial_block_buffer_length;
        uint32_t total = ISAL_SHA256_BLOCK_SIZE;

        memcpy(pad, ctx->partial_block_buffer, used);
        pad[used++] = 0x80;
        if (used > ISAL_SHA256_BLOCK_SIZE - 8)
                total = ISAL_SHA256_BLOCK_SIZE * 2;
        sha256_store_bit_length(&pad[total - 8], ctx->total_length << 3);

        sha256_hash_blocks_zen2(ctx->job.result_digest, pad, total / ISAL_SHA256_BLOCK_SIZE);
        ctx->status = ISAL_HASH_CTX_STS_COMPLETE;
}

int
isal_aes_keyexp_128(const uint8_t *key, uint8_t *exp_key_enc, uint8_t *exp_key_dec)
{
        if (key == NULL)
                return ISAL_CRYPTO_ERR_NULL_KEY;
        if (exp_key_enc == NULL || exp_key_dec == NULL)
                return ISAL_CRYPTO_ERR_NULL_EXP_KEY;

        _aes_keyexp_128_avx(key, exp_key_enc, exp_key_dec);

        return 0;
}

int
isal_aes_cbc_dec_128(const void *in, const void *iv, const void *keys, void *out,
                     const uint64_t len_bytes)
{
        if (keys == NULL)
                return ISAL_CRYPTO_ERR_NULL_EXP_KEY;
        if (in == NULL)
                return ISAL_CRYPTO_ERR_NULL_SRC;
        if (out == NULL)
                return ISAL_CRYPTO_ERR_NULL_DST;
        if (iv == NULL)
                return ISAL_CRYPTO_ERR_NULL_IV;
        if ((len_bytes & 0xf) != 0)
                return ISAL_CRYPTO_ERR_CIPH_LEN;

        _aes_cbc_dec_128_avx((void *) in, (uint8_t *) iv, (uint8_t *) keys, out, len_bytes);

        return 0;
}

int
isal_aes_xts_enc_128_expanded_key(const uint8_t *k2, const uint8_t *k1,
                                  const uint8_t *initial_tweak, const uint64_t len_bytes,
                                  const void *in, void *out)
{
        if (k2 == NULL || k1 == NULL)
                return ISAL_CRYPTO_ERR_NULL_EXP_KEY;
        if (initial_tweak == NULL)
                return ISAL_CRYPTO_ERR_XTS_NULL_TWEAK;
        if (in == NULL)
                return ISAL_CRYPTO_ERR_NULL_SRC;
        if (out == NULL)
                return ISAL_CRYPTO_ERR_NULL_DST;
        if (len_bytes < ISAL_AES_XTS_MIN_LEN || len_bytes > ISAL_AES_XTS_MAX_LEN)
                return ISAL_CRYPTO_ERR_CIPH_LEN;

        _XTS_AES_128_enc_expanded_key_avx((uint8_t *) k2, (uint8_t *) k1,
                                          (uint8_t *) initial_tweak, len_bytes, in, out);

        return 0;
}

int
isal_aes_xts_dec_128_expanded_key(const uint8_t *k2, const uint8_t *k1,
                                  const uint8_t *initial_tweak, const uint64_t len_bytes,
                                  const void *in, void *out)
{
        if (k2 == NULL || k1 == NULL)
                return ISAL_CRYPTO_ERR_NULL_EXP_KEY;
        if (initial_tweak == NULL)
                return ISAL_CRYPTO_ERR_XTS_NULL_TWEAK;
        if (in == NULL)
                return ISAL_CRYPTO_ERR_NULL_SRC;
        if (out == NULL)
                return ISAL_CRYPTO_ERR_NULL_DST;
        if (len_bytes < ISAL_AES_XTS_MIN_LEN || len_bytes > ISAL_AES_XTS_MAX_LEN)
                return ISAL_CRYPTO_ERR_CIPH_LEN;

        _XTS_AES_128_dec_expanded_key_avx((uint8_t *) k2, (uint8_t *) k1,
                                          (uint8_t *) initial_tweak, len_bytes, in, out);

        return 0;
}

int
isal_sha256_ctx_submit_single(ISAL_SHA256_HASH_CTX *ctx, const void *buffer, const uint32_t len,
                              const ISAL_HASH_CTX_FLAG flags)
{
        if (ctx == NULL)
                return ISAL_CRYPTO_ERR_NULL_CTX;
        if (buffer == NULL && len != 0) {
                return ISAL_CRYPTO_ERR_NULL_SRC;
        }
        if (flags & (~ISAL_HASH_ENTIRE)) {
                ctx->error = ISAL_HASH_CTX_ERROR_INVALID_FLAGS;
                return ISAL_CRYPTO_ERR_INVALID_FLAGS;
        }
        if ((ctx->status & ISAL_HASH_CTX_STS_PROCESSING) && flags == ISAL_HASH_ENTIRE) {
                ctx->error = ISAL_HASH_CTX_ERROR_ALREADY_PROCESSING;
                return ISAL_CRYPTO_ERR_ALREADY_PROCESSING;
        }
        if ((ctx->status & ISAL_HASH_CTX_STS_COMPLETE) && !(flags & ISAL_HASH_FIRST)) {
                ctx->error = ISAL_HASH_CTX_ERROR_ALREADY_COMPLETED;
                return ISAL_CRYPTO_ERR_ALREADY_COMPLETED;
        }

        if (flags == ISAL_HASH_FIRST) {
                sha256_ctx_init(ctx);
                sha256_ctx_update(ctx, buffer, len);
                return 0;
        }
        if (flags == ISAL_HASH_UPDATE) {
                sha256_ctx_update(ctx, buffer, len);
                return 0;
        }
        if (flags == ISAL_HASH_LAST) {
                sha256_ctx_update(ctx, buffer, len);
                sha256_ctx_final(ctx);
                return 0;
        }
        sha256_ctx_init(ctx);
        sha256_ctx_update(ctx, buffer, len);
        sha256_ctx_final(ctx);
        return 0;
}
