/* ChaCha20-Poly1305 (RFC 8439) for AVA1 frames, written for speed on the console's
 * Zen 2: ChaCha20 runs 8 blocks at a time in AVX2 (ava1_chacha_avx2.c, chosen at run time
 * by CPUID, so the same binary is safe on any x86-64), Poly1305 uses 44-bit limbs in
 * 64-bit words with 128-bit products. Every other host (the arm64 test Mac) gets the
 * portable path. Byte-for-byte the same result as Monocypher's crypto_aead_*, which stays
 * linked for X25519/BLAKE2b and as the test oracle. No heap; key material is wiped. */
#ifndef AVA1_AEAD_H
#define AVA1_AEAD_H

#include <stddef.h>
#include <stdint.h>

/* Encrypts buf in place and writes the 16-byte tag. */
void ava1_aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                    uint8_t *buf, size_t len, uint8_t mac[16]);
/* 0 and buf decrypted in place, or -1 (tag mismatch, compared in constant time) with buf
 * left exactly as it came in. */
int ava1_aead_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                   uint8_t *buf, size_t len, const uint8_t mac[16]);

/* "avx2" or "portable": the ChaCha20 path in use (crypto.bench reports it). */
const char *ava1_aead_backend(void);

/* The two primitives, for the RFC 8439 vectors (§2.4.2, §2.5.2) and tests. The block
 * counter wraps within 32 bits (Monocypher carries into the nonce instead); AVA1 frames
 * use at most about 2^18 blocks, so the difference is never reached. */
void ava1_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t *buf,
                       size_t len);
void ava1_poly1305(uint8_t mac[16], const uint8_t *msg, size_t len, const uint8_t key[32]);
/* The ChaCha20 block function (§2.3.2): 64 bytes of keystream. */
void ava1_chacha20_block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                         uint8_t out[64]);

/* Tests only: 0 forces the portable path, 1 restores CPUID selection. */
void ava1_aead_allow_simd(int allow);

/* Internal, in ava1_chacha_avx2.c (x86-64 only, built with -mavx2): XORs 512 * groups
 * bytes of buf with keystream blocks st[12], st[12] + 1, ... and advances st[12]. */
void ava1_chacha20_avx2_xor(uint32_t st[16], uint8_t *buf, size_t groups);

#endif
