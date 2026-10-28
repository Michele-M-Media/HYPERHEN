/* ava1_aead.c against the RFC 8439 vectors and against Monocypher (the implementation it
 * replaced), on every ChaCha20 path this CPU can run, then a throughput line. Built and run
 * by scripts/ava1-aead-test.sh (`make test-ava1`): natively, and as x86-64 under Rosetta 2
 * on an arm64 Mac so the AVX2 path is exercised there too.
 *
 *   aead_test          full test + bench
 *   aead_test --bench  bench only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ava1_aead.h"
#include "monocypher.h"

static int g_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            if (++g_fail > 20) exit(1);                                                            \
        }                                                                                          \
    } while (0)

static size_t unhex(const char *s, uint8_t *out) {
    size_t n = 0;
    int hi = -1;
    for (; *s; s++) {
        int v;
        if (*s >= '0' && *s <= '9') v = *s - '0';
        else if (*s >= 'a' && *s <= 'f') v = *s - 'a' + 10;
        else continue;
        if (hi < 0) hi = v;
        else {
            out[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return n;
}

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void) { /* splitmix64 */
    uint64_t z = (g_rng += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static void fill(uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)rnd();
}

static const char SUNSCREEN[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one "
                                "tip for the future, sunscreen would be it.";

static void rfc_vectors(void) {
    uint8_t key[32], nonce[12], out[256], want[256], ad[12], mac[16];
    size_t n, i;
    for (i = 0; i < 32; i++) key[i] = (uint8_t)i;

    /* §2.3.2: the block function. */
    unhex("000000090000004a00000000", nonce);
    ava1_chacha20_block(key, nonce, 1, out);
    n = unhex("10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
              "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e",
              want);
    CHECK(n == 64 && memcmp(out, want, 64) == 0, "RFC 8439 2.3.2 block");

    /* §2.4.2: encryption, counter 1. */
    unhex("000000000000004a00000000", nonce);
    n = sizeof SUNSCREEN - 1;
    memcpy(out, SUNSCREEN, n);
    ava1_chacha20_xor(key, nonce, 1, out, n);
    CHECK(unhex("6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
                "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
                "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
                "5af90bbf74a35be6b40b8eedf2785e42874d",
                want) == n &&
              memcmp(out, want, n) == 0,
          "RFC 8439 2.4.2 encryption");

    /* §2.5.2: Poly1305. */
    {
        static const char msg[] = "Cryptographic Forum Research Group";
        uint8_t pk[32];
        unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", pk);
        ava1_poly1305(mac, (const uint8_t *)msg, sizeof msg - 1, pk);
        unhex("a8061dc1305136c6c22b8baf0c0127a9", want);
        CHECK(memcmp(mac, want, 16) == 0, "RFC 8439 2.5.2 poly1305");
    }

    /* Appendix A.3 #5-#11: Poly1305 edge cases random inputs never reach (h >= p, the
     * 2^130-5 wraparound, carry propagation). */
    {
        static const struct {
            const char *key, *msg, *tag;
        } a3[] = {
            {"02000000000000000000000000000000" "00000000000000000000000000000000",
             "ffffffffffffffffffffffffffffffff", "03000000000000000000000000000000"},
            {"02000000000000000000000000000000" "ffffffffffffffffffffffffffffffff",
             "02000000000000000000000000000000", "03000000000000000000000000000000"},
            {"01000000000000000000000000000000" "00000000000000000000000000000000",
             "ffffffffffffffffffffffffffffffff" "f0ffffffffffffffffffffffffffffff"
             "11000000000000000000000000000000",
             "05000000000000000000000000000000"},
            {"01000000000000000000000000000000" "00000000000000000000000000000000",
             "ffffffffffffffffffffffffffffffff" "fbfefefefefefefefefefefefefefefe"
             "01010101010101010101010101010101",
             "00000000000000000000000000000000"},
            {"02000000000000000000000000000000" "00000000000000000000000000000000",
             "fdffffffffffffffffffffffffffffff", "faffffffffffffffffffffffffffffff"},
            {"01000000000000000400000000000000" "00000000000000000000000000000000",
             "e33594d7505e43b90000000000000000" "3394d7505e4379cd0100000000000000"
             "00000000000000000000000000000000" "01000000000000000000000000000000",
             "14000000000000005500000000000000"},
            {"01000000000000000400000000000000" "00000000000000000000000000000000",
             "e33594d7505e43b90000000000000000" "3394d7505e4379cd0100000000000000"
             "00000000000000000000000000000000",
             "13000000000000000000000000000000"},
        };
        uint8_t pk[32], msg[64];
        for (i = 0; i < sizeof a3 / sizeof a3[0]; i++) {
            unhex(a3[i].key, pk);
            n = unhex(a3[i].msg, msg);
            unhex(a3[i].tag, want);
            ava1_poly1305(mac, msg, n, pk);
            CHECK(memcmp(mac, want, 16) == 0, "RFC 8439 A.3 poly1305 edge vector");
        }
    }

    /* §2.8.2: the AEAD. */
    for (i = 0; i < 32; i++) key[i] = (uint8_t)(0x80 + i);
    unhex("070000004041424344454647", nonce);
    unhex("50515253c0c1c2c3c4c5c6c7", ad);
    n = sizeof SUNSCREEN - 1;
    memcpy(out, SUNSCREEN, n);
    ava1_aead_seal(key, nonce, ad, 12, out, n, mac);
    CHECK(unhex("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
                "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
                "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
                "3ff4def08e4b7a9de576d26586cec64b6116",
                want) == n &&
              memcmp(out, want, n) == 0,
          "RFC 8439 2.8.2 ciphertext");
    unhex("1ae10b594f09e26a7e902ecbd0600691", want);
    CHECK(memcmp(mac, want, 16) == 0, "RFC 8439 2.8.2 tag");
    CHECK(ava1_aead_open(key, nonce, ad, 12, out, n, mac) == 0 && memcmp(out, SUNSCREEN, n) == 0,
          "RFC 8439 2.8.2 open");
}

/* Seal with both implementations; open; refuse flips. */
static void one(size_t len, uint8_t *pt, uint8_t *a, uint8_t *b, int flips) {
    uint8_t key[32], nonce[12], ad[40], mac_a[16], mac_b[16];
    size_t ad_len = (size_t)(rnd() % (sizeof ad + 1)), i;
    crypto_aead_ctx c;
    fill(key, 32);
    fill(nonce, 12);
    fill(ad, sizeof ad);
    fill(pt, len);
    memcpy(a, pt, len);
    ava1_aead_seal(key, nonce, ad, ad_len, a, len, mac_a);
    crypto_aead_init_ietf(&c, key, nonce);
    crypto_aead_write(&c, b, mac_b, ad, ad_len, pt, len);
    CHECK(memcmp(a, b, len) == 0, "len %zu ad %zu: ciphertext differs from Monocypher", len, ad_len);
    CHECK(memcmp(mac_a, mac_b, 16) == 0, "len %zu ad %zu: tag differs from Monocypher", len, ad_len);
    for (i = 0; i < (size_t)flips; i++) {
        size_t span = len + 16 + ad_len, at = (size_t)(rnd() % span);
        uint8_t bit = (uint8_t)(1u << (rnd() % 8)), m[16];
        memcpy(m, mac_a, 16);
        if (at < len) a[at] ^= bit;
        else if (at < len + 16) m[at - len] ^= bit;
        else ad[at - len - 16] ^= bit;
        CHECK(ava1_aead_open(key, nonce, ad, ad_len, a, len, m) != 0, "len %zu: flip at %zu accepted", len,
              at);
        /* A refused frame comes back untouched (still the flipped ciphertext). */
        if (at < len) {
            a[at] ^= bit;
            CHECK(memcmp(a, b, len) == 0, "len %zu: refused open changed the buffer", len);
        } else if (at >= len + 16) {
            ad[at - len - 16] ^= bit;
            CHECK(memcmp(a, b, len) == 0, "len %zu: refused open changed the buffer", len);
        }
    }
    nonce[0] ^= 1;
    CHECK(ava1_aead_open(key, nonce, ad, ad_len, a, len, mac_a) != 0, "len %zu: wrong nonce accepted", len);
    CHECK(memcmp(a, b, len) == 0, "len %zu: refused open changed the buffer", len);
    nonce[0] ^= 1;
    CHECK(ava1_aead_open(key, nonce, ad, ad_len, a, len, mac_a) == 0, "len %zu: open refused", len);
    CHECK(memcmp(a, pt, len) == 0, "len %zu: open did not round-trip", len);
}

/* Raw ChaCha20 at arbitrary counters against Monocypher (exercises the AVX2 lane counters). */
static void stream(size_t len, uint8_t *pt, uint8_t *a, uint8_t *b) {
    uint8_t key[32], nonce[12];
    uint32_t ctr = (uint32_t)(rnd() % 0x7fffffffu);
    fill(key, 32);
    fill(nonce, 12);
    fill(pt, len);
    memcpy(a, pt, len);
    ava1_chacha20_xor(key, nonce, ctr, a, len);
    crypto_chacha20_ietf(b, pt, len, key, nonce, ctr);
    CHECK(memcmp(a, b, len) == 0, "chacha len %zu ctr %u differs", len, ctr);
    {
        uint8_t m1[16], m2[16], pk[32];
        fill(pk, 32);
        ava1_poly1305(m1, pt, len, pk);
        crypto_poly1305(m2, pt, len, pk);
        CHECK(memcmp(m1, m2, 16) == 0, "poly1305 len %zu differs", len);
    }
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* MB/s of f over `rounds` 1 MiB frames. */
#define RATE(rounds, t0) ((double)(rounds) * 1048576.0 / 1e6 / (now_s() - (t0)))

static void bench(const char *label, int rounds) {
    const size_t MIB = 1u << 20;
    uint8_t *buf = malloc(MIB), *ct = malloc(MIB), key[32], nonce[12] = {0}, mac[16];
    double t, seal, open, copy, mono;
    int i;
    crypto_aead_ctx c;
    if (!buf || !ct) exit(1);
    memset(buf, 0x5a, MIB);
    memset(key, 0x11, 32);
    t = now_s();
    for (i = 0; i < rounds; i++) ava1_aead_seal(key, nonce, NULL, 0, buf, MIB, mac);
    seal = RATE(rounds, t);
    /* Open needs the same ciphertext each round: copy it in, and take the copy's time out. */
    memcpy(ct, buf, MIB);
    t = now_s();
    for (i = 0; i < rounds; i++) {
        memcpy(buf, ct, MIB);
        __asm__ __volatile__("" : : "r"(buf) : "memory");
    }
    copy = now_s() - t;
    t = now_s();
    for (i = 0; i < rounds; i++) {
        memcpy(buf, ct, MIB);
        if (ava1_aead_open(key, nonce, NULL, 0, buf, MIB, mac) != 0) {
            fprintf(stderr, "bench: open refused\n");
            exit(1);
        }
    }
    open = (double)rounds * (double)MIB / 1e6 / (now_s() - t - copy);
    t = now_s();
    for (i = 0; i < rounds; i++) {
        crypto_aead_init_ietf(&c, key, nonce);
        crypto_aead_write(&c, buf, mac, NULL, 0, buf, MIB);
    }
    mono = RATE(rounds, t);
    printf("%s: %s seal %.0f MB/s, open %.0f MB/s (Monocypher seal %.0f MB/s), 1 MiB frames, one core\n",
           label, ava1_aead_backend(), seal, open, mono);
    free(buf);
    free(ct);
}

int main(int argc, char **argv) {
    static const size_t big[] = {1101,    2047,     4095,     4096,          4097,           8191,
                                 8192,    8193,     65535,    65536 + 7,     (1u << 20),     (1u << 20) + 63,
                                 3000001, 16777215, 16777216};
    const size_t cap = 16u << 20;
    uint8_t *pt, *a, *b;
    int pass, simd = 0;
    size_t len, k;
    const char *label = sizeof(void *) == 8 ? "host" : "host32";
#if defined(__x86_64__)
    label = "x86_64";
#elif defined(__aarch64__)
    label = "arm64";
#endif
    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        bench(label, 256);
        return 0;
    }
    pt = malloc(cap);
    a = malloc(cap);
    b = malloc(cap);
    if (!pt || !a || !b) return 1;
    simd = strcmp(ava1_aead_backend(), "avx2") == 0;
#if defined(__x86_64__) && !defined(AVA1_AEAD_PORTABLE)
    if (!simd) fprintf(stderr, "NOTICE: this x86-64 CPU/OS has no AVX2: only the portable path is tested\n");
#endif
    for (pass = 0; pass < (simd ? 2 : 1); pass++) {
        ava1_aead_allow_simd(pass == 0 ? simd : 0);
        rfc_vectors();
        for (len = 0; len <= 1100; len++) one(len, pt, a, b, 3);
        for (k = 0; k < sizeof big / sizeof big[0]; k++) one(big[k], pt, a, b, 2);
        for (len = 0; len <= 1100; len += 7) stream(len, pt, a, b);
        for (k = 0; k < sizeof big / sizeof big[0]; k++) stream(big[k], pt, a, b);
        printf("%s: %s path: RFC 8439 vectors + Monocypher differential %s\n", label, ava1_aead_backend(),
               g_fail ? "FAILED" : "ok");
    }
    ava1_aead_allow_simd(1);
    free(pt);
    free(a);
    free(b);
    if (g_fail) return 1;
    bench(label, 256);
    return 0;
}
