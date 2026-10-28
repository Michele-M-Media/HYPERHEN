#include "ava1_aead.h"

#include <string.h>

/* Define AVA1_AEAD_PORTABLE to leave the AVX2 path out entirely (sanitizer/fuzz builds that
 * compile everything in one command without -mavx2 for the AVX2 file). */
#if defined(__x86_64__) && !defined(AVA1_AEAD_PORTABLE)
#define AVA1_HAVE_AVX2 1
#else
#define AVA1_HAVE_AVX2 0
#endif

/* Bytes sealed per step: ChaCha20 then Poly1305 over the same chunk while it is still in
 * L1/L2, instead of two passes over a whole 1 MiB frame. A multiple of 512 (one AVX2 group). */
#define CHUNK 8192u

static void wipe(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) *v++ = 0;
}

static uint32_t ld32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t ld64(const uint8_t *p) { return (uint64_t)ld32(p) | (uint64_t)ld32(p + 4) << 32; }

static void st32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void st64(uint8_t *p, uint64_t v) {
    st32(p, (uint32_t)v);
    st32(p + 4, (uint32_t)(v >> 32));
}

/* ---- run-time selection ------------------------------------------------------------ */

#if AVA1_HAVE_AVX2
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    __asm__ __volatile__("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}

/* AVX2 needs the CPU flag AND the OS saving the YMM state (OSXSAVE + XCR0 bits 1 and 2). */
static int detect_avx2(void) {
    uint32_t r[4], lo, hi;
    cpuid(0, 0, r);
    if (r[0] < 7) return 0;
    cpuid(1, 0, r);
    if (!(r[2] & (1u << 27)) || !(r[2] & (1u << 28))) return 0; /* OSXSAVE, AVX */
    __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    (void)hi;
    if ((lo & 6u) != 6u) return 0;
    cpuid(7, 0, r);
    return (r[1] & (1u << 5)) != 0; /* EBX bit 5 = AVX2 */
}
#endif

#if AVA1_HAVE_AVX2
static int g_cpu = -1; /* -1 unknown, 0 portable, 1 AVX2; probed once */
#endif
static int g_allow = 1;

static int use_avx2(void) {
#if AVA1_HAVE_AVX2
    int c = __atomic_load_n(&g_cpu, __ATOMIC_RELAXED);
    if (c < 0) {
        c = detect_avx2();
        __atomic_store_n(&g_cpu, c, __ATOMIC_RELAXED);
    }
    return c && __atomic_load_n(&g_allow, __ATOMIC_RELAXED);
#else
    return 0;
#endif
}

void ava1_aead_allow_simd(int allow) { __atomic_store_n(&g_allow, allow ? 1 : 0, __ATOMIC_RELAXED); }

const char *ava1_aead_backend(void) { return use_avx2() ? "avx2" : "portable"; }

/* ---- ChaCha20 (RFC 8439 §2.3) ------------------------------------------------------ */

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define QR(a, b, c, d)                                                                             \
    do {                                                                                           \
        a += b; d ^= a; d = ROTL(d, 16);                                                           \
        c += d; b ^= c; b = ROTL(b, 12);                                                           \
        a += b; d ^= a; d = ROTL(d, 8);                                                            \
        c += d; b ^= c; b = ROTL(b, 7);                                                            \
    } while (0)

static void chacha_init(uint32_t st[16], const uint8_t key[32], const uint8_t nonce[12], uint32_t ctr) {
    int i;
    st[0] = 0x61707865u;
    st[1] = 0x3320646eu;
    st[2] = 0x79622d32u;
    st[3] = 0x6b206574u;
    for (i = 0; i < 8; i++) st[4 + i] = ld32(key + 4 * i);
    st[12] = ctr;
    for (i = 0; i < 3; i++) st[13 + i] = ld32(nonce + 4 * i);
}

static void chacha_block(const uint32_t st[16], uint32_t out[16]) {
    uint32_t x0 = st[0], x1 = st[1], x2 = st[2], x3 = st[3], x4 = st[4], x5 = st[5], x6 = st[6],
             x7 = st[7], x8 = st[8], x9 = st[9], x10 = st[10], x11 = st[11], x12 = st[12],
             x13 = st[13], x14 = st[14], x15 = st[15];
    int i;
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
    out[0] = x0 + st[0];
    out[1] = x1 + st[1];
    out[2] = x2 + st[2];
    out[3] = x3 + st[3];
    out[4] = x4 + st[4];
    out[5] = x5 + st[5];
    out[6] = x6 + st[6];
    out[7] = x7 + st[7];
    out[8] = x8 + st[8];
    out[9] = x9 + st[9];
    out[10] = x10 + st[10];
    out[11] = x11 + st[11];
    out[12] = x12 + st[12];
    out[13] = x13 + st[13];
    out[14] = x14 + st[14];
    out[15] = x15 + st[15];
}

/* XORs len bytes of keystream from block st[12] on; st[12] advances by the full blocks
 * used (a partial last block only ever ends a message). */
static void chacha_xor(uint32_t st[16], uint8_t *buf, size_t len) {
    uint32_t ks[16];
    uint8_t kb[64];
    size_t i;
#if AVA1_HAVE_AVX2
    if (use_avx2()) {
        size_t groups = len / 512;
        if (groups) {
            ava1_chacha20_avx2_xor(st, buf, groups);
            buf += groups * 512;
            len -= groups * 512;
        }
        /* A tail of 3+ blocks is cheaper as one more AVX2 group run on a copy. */
        if (len >= 192) {
            uint8_t tmp[512];
            memset(tmp, 0, sizeof tmp);
            memcpy(tmp, buf, len);
            ava1_chacha20_avx2_xor(st, tmp, 1);
            memcpy(buf, tmp, len);
            wipe(tmp, sizeof tmp);
            return; /* the message ends here; st[12] is past it either way */
        }
    }
#endif
    while (len >= 64) {
        chacha_block(st, ks);
        for (i = 0; i < 16; i++) st32(buf + 4 * i, ld32(buf + 4 * i) ^ ks[i]);
        st[12]++;
        buf += 64;
        len -= 64;
    }
    if (len) {
        chacha_block(st, ks);
        for (i = 0; i < 16; i++) st32(kb + 4 * i, ks[i]);
        for (i = 0; i < len; i++) buf[i] ^= kb[i];
        st[12]++;
        wipe(kb, sizeof kb);
    }
    wipe(ks, sizeof ks);
}

void ava1_chacha20_block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                         uint8_t out[64]) {
    uint32_t st[16], ks[16];
    int i;
    chacha_init(st, key, nonce, counter);
    chacha_block(st, ks);
    for (i = 0; i < 16; i++) st32(out + 4 * i, ks[i]);
    wipe(st, sizeof st);
    wipe(ks, sizeof ks);
}

void ava1_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t *buf,
                       size_t len) {
    uint32_t st[16];
    chacha_init(st, key, nonce, counter);
    chacha_xor(st, buf, len);
    wipe(st, sizeof st);
}

/* ---- Poly1305 (RFC 8439 §2.5), 44/44/42-bit limbs (poly1305-donna-64) --------------- */

typedef unsigned __int128 u128;

#define M44 0xfffffffffffull
#define M42 0x3ffffffffffull

typedef struct {
    uint64_t r0, r1, r2, s1, s2; /* r clamped; s = r * 20 (5 * 4, for the 44-bit wrap) */
    uint64_t h0, h1, h2;
    uint64_t pad0, pad1;
    uint8_t buf[16];
    size_t used;
} poly_t;

static void poly_init(poly_t *p, const uint8_t key[32]) {
    uint64_t t0 = ld64(key), t1 = ld64(key + 8);
    p->r0 = t0 & 0xffc0fffffffull;
    p->r1 = ((t0 >> 44) | (t1 << 20)) & 0xfffffc0ffffull;
    p->r2 = (t1 >> 24) & 0x00ffffffc0full;
    p->s1 = p->r1 * (5 << 2);
    p->s2 = p->r2 * (5 << 2);
    p->h0 = p->h1 = p->h2 = 0;
    p->pad0 = ld64(key + 16);
    p->pad1 = ld64(key + 24);
    p->used = 0;
}

/* n is a multiple of 16; hibit is 2^128 in limb 2 for every full block. */
static void poly_blocks(poly_t *p, const uint8_t *m, size_t n, uint64_t hibit) {
    const uint64_t r0 = p->r0, r1 = p->r1, r2 = p->r2, s1 = p->s1, s2 = p->s2;
    uint64_t h0 = p->h0, h1 = p->h1, h2 = p->h2, c, t0, t1;
    u128 d0, d1, d2;
    while (n >= 16) {
        t0 = ld64(m);
        t1 = ld64(m + 8);
        h0 += t0 & M44;
        h1 += ((t0 >> 44) | (t1 << 20)) & M44;
        h2 += ((t1 >> 24) & M42) | hibit;
        d0 = (u128)h0 * r0 + (u128)h1 * s2 + (u128)h2 * s1;
        d1 = (u128)h0 * r1 + (u128)h1 * r0 + (u128)h2 * s2;
        d2 = (u128)h0 * r2 + (u128)h1 * r1 + (u128)h2 * r0;
        c = (uint64_t)(d0 >> 44);
        h0 = (uint64_t)d0 & M44;
        d1 += c;
        c = (uint64_t)(d1 >> 44);
        h1 = (uint64_t)d1 & M44;
        d2 += c;
        c = (uint64_t)(d2 >> 42);
        h2 = (uint64_t)d2 & M42;
        h0 += c * 5;
        c = h0 >> 44;
        h0 &= M44;
        h1 += c;
        m += 16;
        n -= 16;
    }
    p->h0 = h0;
    p->h1 = h1;
    p->h2 = h2;
}

static void poly_update(poly_t *p, const uint8_t *m, size_t n) {
    size_t take, full;
    if (p->used) {
        take = 16 - p->used < n ? 16 - p->used : n;
        memcpy(p->buf + p->used, m, take);
        p->used += take;
        m += take;
        n -= take;
        if (p->used < 16) return;
        poly_blocks(p, p->buf, 16, 1ull << 40);
        p->used = 0;
    }
    full = n & ~(size_t)15;
    if (full) poly_blocks(p, m, full, 1ull << 40);
    if (n > full) {
        memcpy(p->buf, m + full, n - full);
        p->used = n - full;
    }
}

/* Zero padding to the next 16-byte boundary (the AEAD's pad16). */
static void poly_pad16(poly_t *p) {
    if (p->used) {
        memset(p->buf + p->used, 0, 16 - p->used);
        poly_blocks(p, p->buf, 16, 1ull << 40);
        p->used = 0;
    }
}

static void poly_final(poly_t *p, uint8_t mac[16]) {
    uint64_t h0, h1, h2, g0, g1, g2, c, t0, t1;
    if (p->used) { /* a short last block: 0x01 then zeros, no 2^128 bit */
        p->buf[p->used] = 1;
        memset(p->buf + p->used + 1, 0, 15 - p->used);
        poly_blocks(p, p->buf, 16, 0);
    }
    h0 = p->h0;
    h1 = p->h1;
    h2 = p->h2;
    c = h1 >> 44; h1 &= M44; h2 += c;
    c = h2 >> 42; h2 &= M42; h0 += c * 5;
    c = h0 >> 44; h0 &= M44; h1 += c;
    c = h1 >> 44; h1 &= M44; h2 += c;
    c = h2 >> 42; h2 &= M42; h0 += c * 5;
    c = h0 >> 44; h0 &= M44; h1 += c;
    /* g = h + 5 - 2^130; take g when it did not go negative (h >= p) */
    g0 = h0 + 5; c = g0 >> 44; g0 &= M44;
    g1 = h1 + c; c = g1 >> 44; g1 &= M44;
    g2 = h2 + c - (1ull << 42);
    c = (g2 >> 63) - 1; /* all ones when g2 >= 0 */
    g0 &= c;
    g1 &= c;
    g2 &= c;
    c = ~c;
    h0 = (h0 & c) | g0;
    h1 = (h1 & c) | g1;
    h2 = (h2 & c) | g2;
    /* h + s mod 2^128 */
    t0 = p->pad0;
    t1 = p->pad1;
    h0 += t0 & M44; c = h0 >> 44; h0 &= M44;
    h1 += (((t0 >> 44) | (t1 << 20)) & M44) + c; c = h1 >> 44; h1 &= M44;
    h2 += ((t1 >> 24) & M42) + c; h2 &= M42;
    st64(mac, h0 | (h1 << 44));
    st64(mac + 8, (h1 >> 20) | (h2 << 24));
    wipe(p, sizeof *p);
}

void ava1_poly1305(uint8_t mac[16], const uint8_t *msg, size_t len, const uint8_t key[32]) {
    poly_t p;
    poly_init(&p, key);
    poly_update(&p, msg, len);
    poly_final(&p, mac);
}

/* ---- the AEAD (RFC 8439 §2.8) ------------------------------------------------------ */

static void aead_begin(uint32_t st[16], poly_t *p, const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *ad, size_t ad_len) {
    uint32_t ks[16];
    uint8_t otk[32];
    int i;
    chacha_init(st, key, nonce, 0);
    chacha_block(st, ks); /* block 0: the one-time Poly1305 key */
    for (i = 0; i < 8; i++) st32(otk + 4 * i, ks[i]);
    poly_init(p, otk);
    wipe(ks, sizeof ks);
    wipe(otk, sizeof otk);
    if (ad_len) poly_update(p, ad, ad_len);
    poly_pad16(p);
    st[12] = 1;
}

static void aead_end(poly_t *p, size_t ad_len, size_t len, uint8_t mac[16]) {
    uint8_t lens[16];
    poly_pad16(p);
    st64(lens, (uint64_t)ad_len);
    st64(lens + 8, (uint64_t)len);
    poly_update(p, lens, 16);
    poly_final(p, mac);
}

void ava1_aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                    uint8_t *buf, size_t len, uint8_t mac[16]) {
    uint32_t st[16];
    poly_t p;
    size_t off, n;
    aead_begin(st, &p, key, nonce, ad, ad_len);
    for (off = 0; off < len; off += n) {
        n = len - off < CHUNK ? len - off : CHUNK;
        chacha_xor(st, buf + off, n);
        poly_update(&p, buf + off, n);
    }
    aead_end(&p, ad_len, len, mac);
    wipe(st, sizeof st);
}

int ava1_aead_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t ad_len,
                   uint8_t *buf, size_t len, const uint8_t mac[16]) {
    uint32_t st[16];
    poly_t p;
    uint8_t want[16], tag[16];
    unsigned diff = 0;
    size_t off, n;
    int i;
    memcpy(tag, mac, sizeof tag); /* before buf changes, in case a caller's mac lies inside it */
    /* MAC and decrypt each chunk while it is hot. A bad tag re-applies the same keystream,
     * which puts the ciphertext back: the caller never sees unauthenticated plaintext and
     * buf ends exactly as it came in, as with Monocypher's verify-then-decrypt. */
    aead_begin(st, &p, key, nonce, ad, ad_len);
    for (off = 0; off < len; off += n) {
        n = len - off < CHUNK ? len - off : CHUNK;
        poly_update(&p, buf + off, n);
        chacha_xor(st, buf + off, n);
    }
    aead_end(&p, ad_len, len, want);
    for (i = 0; i < 16; i++) diff |= (unsigned)(want[i] ^ tag[i]);
    wipe(want, sizeof want);
    if (diff != 0) {
        st[12] = 1;
        chacha_xor(st, buf, len);
    }
    wipe(st, sizeof st);
    return diff == 0 ? 0 : -1;
}
