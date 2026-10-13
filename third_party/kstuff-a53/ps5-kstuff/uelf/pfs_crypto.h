#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sha256.h"

enum {
    PFS_AES128_EXPKEY_SIZE = 16 * 11,
    PFS_CRYPTO_KEY_SIZE = 32,
    PFS_HMAC_SHA256_CACHE_SLOTS = 2,
    PFS_XTS_KEY_CACHE_SLOTS = 2,
};

struct xts_key_cache_entry
{
    int key_id;
    uint8_t key[PFS_CRYPTO_KEY_SIZE];
    uint8_t data_key_enc[PFS_AES128_EXPKEY_SIZE] __attribute__((aligned(16)));
    uint8_t data_key_dec[PFS_AES128_EXPKEY_SIZE] __attribute__((aligned(16)));
    uint8_t tweak_key_enc[PFS_AES128_EXPKEY_SIZE] __attribute__((aligned(16)));
    uint8_t tweak_key_dec[PFS_AES128_EXPKEY_SIZE] __attribute__((aligned(16)));
    uint8_t valid;
};

struct hmac_sha256_cache_entry
{
    int key_id;
    uint8_t key[PFS_CRYPTO_KEY_SIZE];
    struct uelf_sha256_context inner_ctx;
    struct uelf_sha256_context outer_ctx;
    uint8_t valid;
};

struct crypto_request_cache
{
    struct xts_key_cache_entry xts[PFS_XTS_KEY_CACHE_SLOTS];
    struct hmac_sha256_cache_entry hmac[PFS_HMAC_SHA256_CACHE_SLOTS];
    uint8_t xts_next_slot;
    uint8_t hmac_next_slot;
};

int pfs_derive_fake_keys(const uint8_t* p_eekpfs, const uint8_t* crypt_seed, uint8_t* ek, uint8_t* sk);
int pfs_hmac_virtual(struct crypto_request_cache* cache, uint8_t* out, int key_id, const uint8_t* key,
                     uint64_t data, size_t data_size);
int pfs_hmac_virtual_fpu_held(struct crypto_request_cache* cache, uint8_t* out, int key_id, const uint8_t* key,
                              uint64_t data, size_t data_size);
int pfs_xts_virtual(struct crypto_request_cache* cache, uint64_t dst, uint64_t src, int key_id,
                    const uint8_t* key, uint64_t start, uint32_t count, int is_encrypt);
int pfs_xts_virtual_fpu_held(struct crypto_request_cache* cache, uint64_t dst, uint64_t src, int key_id,
                             const uint8_t* key, uint64_t start, uint32_t count, int is_encrypt);
