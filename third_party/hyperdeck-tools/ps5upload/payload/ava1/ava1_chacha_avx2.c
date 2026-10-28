/* ChaCha20, 8 blocks per step in AVX2 (one block per 32-bit lane), for ava1_aead.c. Its
 * own translation unit so only this file is compiled with -mavx2: ava1_aead.c calls it
 * only after CPUID said the CPU and OS support AVX2. On anything but x86-64 (and in builds
 * that define AVA1_AEAD_PORTABLE) it compiles to nothing. */
#include "ava1_aead.h"

#if defined(__x86_64__) && !defined(AVA1_AEAD_PORTABLE)

#ifndef __AVX2__
#error "ava1_chacha_avx2.c must be compiled with -mavx2 (or define AVA1_AEAD_PORTABLE everywhere)"
#endif

#include <immintrin.h>

#define ADD _mm256_add_epi32
#define XOR _mm256_xor_si256
#define ROT(x, n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - (n)))

#define QR(a, b, c, d)                                                                             \
    do {                                                                                           \
        a = ADD(a, b); d = XOR(d, a); d = _mm256_shuffle_epi8(d, r16);                             \
        c = ADD(c, d); b = XOR(b, c); b = ROT(b, 12);                                              \
        a = ADD(a, b); d = XOR(d, a); d = _mm256_shuffle_epi8(d, r8);                              \
        c = ADD(c, d); b = XOR(b, c); b = ROT(b, 7);                                               \
    } while (0)

/* Words a..a+3 of all 8 blocks (one vector per word) → for k = 0..3, out[k] holds words
 * a..a+3 of block k in its low half and of block k + 4 in its high half. */
#define TRANSPOSE4(x0, x1, x2, x3, out)                                                            \
    do {                                                                                           \
        __m256i t0 = _mm256_unpacklo_epi32(x0, x1), t1 = _mm256_unpackhi_epi32(x0, x1);            \
        __m256i t2 = _mm256_unpacklo_epi32(x2, x3), t3 = _mm256_unpackhi_epi32(x2, x3);            \
        out[0] = _mm256_unpacklo_epi64(t0, t2);                                                    \
        out[1] = _mm256_unpackhi_epi64(t0, t2);                                                    \
        out[2] = _mm256_unpacklo_epi64(t1, t3);                                                    \
        out[3] = _mm256_unpackhi_epi64(t1, t3);                                                    \
    } while (0)

static void xor32(uint8_t *p, __m256i ks) {
    _mm256_storeu_si256((__m256i *)p, XOR(_mm256_loadu_si256((const __m256i *)p), ks));
}

void ava1_chacha20_avx2_xor(uint32_t st[16], uint8_t *buf, size_t groups) {
    const __m256i r16 = _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2, 13, 12,
                                        15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    const __m256i r8 = _mm256_set_epi8(14, 13, 12, 15, 10, 9, 8, 11, 6, 5, 4, 7, 2, 1, 0, 3, 14, 13,
                                       12, 15, 10, 9, 8, 11, 6, 5, 4, 7, 2, 1, 0, 3);
    const __m256i lanes = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    __m256i s[16];
    int i;
    for (i = 0; i < 16; i++) s[i] = _mm256_set1_epi32((int)st[i]);
    while (groups--) {
        __m256i x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15;
        __m256i a[4], b[4], c[4], d[4];
        __m256i ctr = ADD(_mm256_set1_epi32((int)st[12]), lanes);
        x0 = s[0]; x1 = s[1]; x2 = s[2]; x3 = s[3];
        x4 = s[4]; x5 = s[5]; x6 = s[6]; x7 = s[7];
        x8 = s[8]; x9 = s[9]; x10 = s[10]; x11 = s[11];
        x12 = ctr; x13 = s[13]; x14 = s[14]; x15 = s[15];
        for (i = 0; i < 10; i++) {
            QR(x0, x4, x8, x12);
            QR(x1, x5, x9, x13);
            QR(x2, x6, x10, x14);
            QR(x3, x7, x11, x15);
            QR(x0, x5, x10, x15);
            QR(x1, x6, x11, x12);
            QR(x2, x7, x8, x13);
            QR(x3, x4, x9, x14);
        }
        x0 = ADD(x0, s[0]); x1 = ADD(x1, s[1]); x2 = ADD(x2, s[2]); x3 = ADD(x3, s[3]);
        x4 = ADD(x4, s[4]); x5 = ADD(x5, s[5]); x6 = ADD(x6, s[6]); x7 = ADD(x7, s[7]);
        x8 = ADD(x8, s[8]); x9 = ADD(x9, s[9]); x10 = ADD(x10, s[10]); x11 = ADD(x11, s[11]);
        x12 = ADD(x12, ctr); x13 = ADD(x13, s[13]); x14 = ADD(x14, s[14]); x15 = ADD(x15, s[15]);
        TRANSPOSE4(x0, x1, x2, x3, a);
        TRANSPOSE4(x4, x5, x6, x7, b);
        TRANSPOSE4(x8, x9, x10, x11, c);
        TRANSPOSE4(x12, x13, x14, x15, d);
        for (i = 0; i < 4; i++) {
            xor32(buf + 64 * i, _mm256_permute2x128_si256(a[i], b[i], 0x20));
            xor32(buf + 64 * i + 32, _mm256_permute2x128_si256(c[i], d[i], 0x20));
            xor32(buf + 64 * (i + 4), _mm256_permute2x128_si256(a[i], b[i], 0x31));
            xor32(buf + 64 * (i + 4) + 32, _mm256_permute2x128_si256(c[i], d[i], 0x31));
        }
        st[12] += 8;
        buf += 512;
    }
    /* Keystream and key-derived state live only in registers and these locals. */
    for (i = 0; i < 16; i++) s[i] = _mm256_setzero_si256();
    __asm__ __volatile__("" : : "r"(s) : "memory");
    _mm256_zeroupper();
}

#else
typedef int ava1_chacha_avx2_unused; /* ISO C: a translation unit must declare something */
#endif
