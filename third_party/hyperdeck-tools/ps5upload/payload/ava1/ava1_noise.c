#include "ava1_noise.h"

#include <string.h>

#include "ava1_aead.h"
#include "ava1_wire.h"
#include "monocypher.h"

#define HASHLEN 64
#define BLOCKLEN 128

static void hash2(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, uint8_t out[HASHLEN]) {
    crypto_blake2b_ctx c;
    crypto_blake2b_init(&c, HASHLEN);
    crypto_blake2b_update(&c, a, an);
    crypto_blake2b_update(&c, b, bn);
    crypto_blake2b_final(&c, out);
}

/* HMAC-BLAKE2b over (a ‖ b). */
static void hmac(const uint8_t key[HASHLEN], const uint8_t *a, size_t an, const uint8_t *b, size_t bn,
                 uint8_t out[HASHLEN]) {
    uint8_t ipad[BLOCKLEN], opad[BLOCKLEN], inner[HASHLEN];
    crypto_blake2b_ctx c;
    int i;
    memset(ipad, 0x36, BLOCKLEN);
    memset(opad, 0x5c, BLOCKLEN);
    for (i = 0; i < HASHLEN; i++) {
        ipad[i] ^= key[i];
        opad[i] ^= key[i];
    }
    crypto_blake2b_init(&c, HASHLEN);
    crypto_blake2b_update(&c, ipad, BLOCKLEN);
    crypto_blake2b_update(&c, a, an);
    crypto_blake2b_update(&c, b, bn);
    crypto_blake2b_final(&c, inner);
    hash2(opad, BLOCKLEN, inner, HASHLEN, out);
    crypto_wipe(ipad, sizeof ipad);
    crypto_wipe(opad, sizeof opad);
    crypto_wipe(inner, sizeof inner);
}

/* HKDF(ck, ikm) → two HASHLEN outputs. */
static void hkdf2(const uint8_t ck[HASHLEN], const uint8_t *ikm, size_t n, uint8_t o1[HASHLEN],
                  uint8_t o2[HASHLEN]) {
    uint8_t tk[HASHLEN];
    static const uint8_t one = 1, two = 2;
    hmac(ck, ikm, n, NULL, 0, tk);
    hmac(tk, &one, 1, NULL, 0, o1);
    hmac(tk, o1, HASHLEN, &two, 1, o2);
    crypto_wipe(tk, sizeof tk);
}

static void mix_hash(ava1_noise_t *ns, const uint8_t *d, size_t n) {
    uint8_t h[HASHLEN];
    hash2(ns->h, HASHLEN, d, n, h);
    memcpy(ns->h, h, HASHLEN);
    crypto_wipe(h, sizeof h);
}

static void mix_key(ava1_noise_t *ns, const uint8_t ikm[32]) {
    uint8_t ck[HASHLEN], tk[HASHLEN];
    hkdf2(ns->ck, ikm, 32, ck, tk);
    memcpy(ns->ck, ck, HASHLEN);
    memcpy(ns->k, tk, 32);
    ns->has_k = 1;
    ns->n = 0;
    crypto_wipe(ck, sizeof ck);
    crypto_wipe(tk, sizeof tk);
}

static void nonce12(uint64_t n, uint8_t out[12]) {
    int i;
    memset(out, 0, 4);
    for (i = 0; i < 8; i++) out[4 + i] = (uint8_t)(n >> (8 * i));
}

void ava1_seal(const uint8_t key[32], uint64_t n, const uint8_t *ad, size_t ad_len, uint8_t *buf,
               size_t len, uint8_t mac[16]) {
    uint8_t nn[12];
    nonce12(n, nn);
    ava1_aead_seal(key, nn, ad, ad_len, buf, len, mac);
    crypto_wipe(nn, sizeof nn);
}

int ava1_open(const uint8_t key[32], uint64_t n, const uint8_t *ad, size_t ad_len, uint8_t *buf,
              size_t len, const uint8_t mac[16]) {
    uint8_t nn[12];
    int rc;
    nonce12(n, nn);
    rc = ava1_aead_open(key, nn, ad, ad_len, buf, len, mac);
    crypto_wipe(nn, sizeof nn);
    return rc == 0 ? 0 : AVA1_E_TAG;
}

/* EncryptAndHash, appending to out at *at. */
static int enc_hash(ava1_noise_t *ns, const uint8_t *p, size_t n, uint8_t *out, size_t cap, size_t *at) {
    size_t need = n + (ns->has_k ? 16 : 0);
    if (need > cap - *at) return AVA1_E_SPACE;
    if (n) memmove(out + *at, p, n);
    if (ns->has_k) ava1_seal(ns->k, ns->n++, ns->h, HASHLEN, out + *at, n, out + *at + n);
    mix_hash(ns, out + *at, need);
    *at += need;
    return 0;
}

/* DecryptAndHash of n plaintext bytes from msg at *at. */
static int dec_hash(ava1_noise_t *ns, const uint8_t *msg, size_t len, size_t *at, size_t n, uint8_t *out) {
    size_t need = n + (ns->has_k ? 16 : 0);
    uint8_t h_before[HASHLEN];
    if (need > len - *at) return AVA1_E_SHORT;
    memcpy(h_before, ns->h, HASHLEN);
    mix_hash(ns, msg + *at, need);
    if (n) memmove(out, msg + *at, n);
    if (ns->has_k && ava1_open(ns->k, ns->n++, h_before, HASHLEN, out, n, msg + *at + n) != 0) {
        if (n) crypto_wipe(out, n); /* unauthenticated plaintext is never handed back */
        return AVA1_E_TAG;
    }
    *at += need;
    return 0;
}

static int dh_mix(ava1_noise_t *ns, const ava1_identity_t *mine, const uint8_t theirs[32]) {
    uint8_t d[32];
    int rc = ava1_dh(mine, theirs, d);
    if (rc == 0) mix_key(ns, d);
    crypto_wipe(d, sizeof d);
    return rc == 0 ? 0 : AVA1_E_PROTO;
}

void ava1_noise_init(ava1_noise_t *ns, int initiator, const ava1_identity_t *s,
                     const ava1_identity_t *e, const uint8_t *prologue, size_t prologue_len) {
    static const char name[] = "Noise_XX_25519_ChaChaPoly_BLAKE2b";
    memset(ns, 0, sizeof *ns);
    memcpy(ns->h, name, sizeof name - 1); /* shorter than HASHLEN, so zero-padded */
    memcpy(ns->ck, ns->h, HASHLEN);
    ns->s = *s;
    ns->e = *e;
    ns->initiator = initiator;
    mix_hash(ns, prologue, prologue_len);
}

/* Messages: 0 "-> e", 1 "<- e, ee, s, es", 2 "-> s, se". */
static int fail(ava1_noise_t *ns, int rc) {
    ns->failed = 1;
    return rc;
}

int ava1_noise_write(ava1_noise_t *ns, const uint8_t *payload, size_t payload_len, uint8_t *out,
                     size_t cap, size_t *out_len) {
    size_t at = 0;
    int rc = 0;
    if (ns->failed) return AVA1_E_PROTO;
    if (ns->step > 2 || (ns->step % 2 == 0) != (ns->initiator != 0)) return fail(ns, AVA1_E_PROTO);
    if (ns->step == 0 || ns->step == 1) {
        if (cap < 32) return fail(ns, AVA1_E_SPACE);
        memcpy(out, ns->e.pub, 32);
        mix_hash(ns, ns->e.pub, 32);
        at = 32;
    }
    if (ns->step == 1) {
        rc = dh_mix(ns, &ns->e, ns->re);                              /* ee */
        if (rc == 0) rc = enc_hash(ns, ns->s.pub, 32, out, cap, &at); /* s */
        if (rc == 0) rc = dh_mix(ns, &ns->s, ns->re);                 /* es */
    } else if (ns->step == 2) {
        rc = enc_hash(ns, ns->s.pub, 32, out, cap, &at);              /* s */
        if (rc == 0) rc = dh_mix(ns, &ns->s, ns->re);                 /* se */
    }
    if (rc == 0) rc = enc_hash(ns, payload, payload_len, out, cap, &at);
    if (rc != 0) return fail(ns, rc);
    ns->step++;
    *out_len = at;
    return 0;
}

int ava1_noise_read(ava1_noise_t *ns, const uint8_t *msg, size_t len, uint8_t *payload, size_t cap,
                    size_t *payload_len) {
    size_t at = 0, mac, n;
    int rc = 0;
    if (ns->failed) return AVA1_E_PROTO;
    if (ns->step > 2 || (ns->step % 2 == 0) == (ns->initiator != 0)) return fail(ns, AVA1_E_PROTO);
    if (ns->step == 0 || ns->step == 1) {
        if (len < 32) return fail(ns, AVA1_E_SHORT);
        memcpy(ns->re, msg, 32);
        mix_hash(ns, ns->re, 32);
        at = 32;
    }
    if (ns->step == 1) {
        rc = dh_mix(ns, &ns->e, ns->re);                              /* ee */
        if (rc == 0) rc = dec_hash(ns, msg, len, &at, 32, ns->rs);    /* s */
        if (rc == 0) rc = dh_mix(ns, &ns->e, ns->rs);                 /* es */
    } else if (ns->step == 2) {
        rc = dec_hash(ns, msg, len, &at, 32, ns->rs);                 /* s */
        if (rc == 0) rc = dh_mix(ns, &ns->e, ns->rs);                 /* se */
    }
    if (rc != 0) return fail(ns, rc);
    mac = ns->has_k ? 16 : 0;
    if (len - at < mac) return fail(ns, AVA1_E_SHORT);
    n = len - at - mac;
    if (n > cap) return fail(ns, AVA1_E_SPACE);
    rc = dec_hash(ns, msg, len, &at, n, payload);
    if (rc != 0) return fail(ns, rc);
    ns->step++;
    *payload_len = n;
    return 0;
}

int ava1_noise_split(const ava1_noise_t *ns, uint8_t k_i2r[32], uint8_t k_r2i[32]) {
    uint8_t a[HASHLEN], b[HASHLEN];
    if (ns->failed || ns->step != 3) {
        /* Keys from an unfinished handshake authenticate nobody. */
        memset(k_i2r, 0, 32);
        memset(k_r2i, 0, 32);
        return AVA1_E_PROTO;
    }
    hkdf2(ns->ck, NULL, 0, a, b);
    memcpy(k_i2r, a, 32);
    memcpy(k_r2i, b, 32);
    crypto_wipe(a, sizeof a);
    crypto_wipe(b, sizeof b);
    return 0;
}

void ava1_noise_wipe(ava1_noise_t *ns) { crypto_wipe(ns, sizeof *ns); }
