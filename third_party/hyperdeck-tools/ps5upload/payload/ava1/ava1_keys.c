#include "ava1_keys.h"

#include <string.h>

#include "ava1_platform.h"
#include "monocypher.h"

void ava1_identity_from_secret(ava1_identity_t *id, const uint8_t secret[32]) {
    memcpy(id->secret, secret, 32);
    crypto_x25519_public_key(id->pub, id->secret);
}

int ava1_dh(const ava1_identity_t *id, const uint8_t peer[32], uint8_t out[32]) {
    uint8_t acc = 0;
    int i;
    crypto_x25519(out, id->secret, peer);
    for (i = 0; i < 32; i++) acc |= out[i];
    return acc ? 0 : -1;
}

void ava1_lane_key(const uint8_t dir[32], uint16_t lane, const uint8_t client_nonce[16],
                   const uint8_t server_nonce[16], uint8_t out[32]) {
    static const char label[] = "AVA1 lane";
    crypto_blake2b_ctx c;
    uint8_t l[2];
    l[0] = (uint8_t)lane;
    l[1] = (uint8_t)(lane >> 8);
    crypto_blake2b_keyed_init(&c, 32, dir, 32);
    crypto_blake2b_update(&c, (const uint8_t *)label, sizeof label - 1);
    crypto_blake2b_update(&c, l, 2);
    crypto_blake2b_update(&c, client_nonce, 16);
    crypto_blake2b_update(&c, server_nonce, 16);
    crypto_blake2b_final(&c, out);
}

void ava1_control_key(const uint8_t dir[32], uint8_t out[32]) {
    static const uint8_t zero[16] = { 0 };
    ava1_lane_key(dir, 0, zero, zero, out);
}

/* keyed BLAKE2b-128(BLAKE2b-256(dir, "AVA1 join"), label ‖ sid ‖ u16le(lane) ‖ cn [‖ sn]). */
static void join_mac(const uint8_t dir[32], const char *label, const uint8_t sid[16], uint16_t lane,
                     const uint8_t cn[16], const uint8_t *sn, uint8_t out[16]) {
    static const char join[] = "AVA1 join";
    crypto_blake2b_ctx c;
    uint8_t jk[32], l[2];
    crypto_blake2b_keyed(jk, 32, dir, 32, (const uint8_t *)join, sizeof join - 1);
    l[0] = (uint8_t)lane;
    l[1] = (uint8_t)(lane >> 8);
    crypto_blake2b_keyed_init(&c, 16, jk, 32);
    crypto_blake2b_update(&c, (const uint8_t *)label, strlen(label));
    crypto_blake2b_update(&c, sid, 16);
    crypto_blake2b_update(&c, l, 2);
    crypto_blake2b_update(&c, cn, 16);
    if (sn) crypto_blake2b_update(&c, sn, 16);
    crypto_blake2b_final(&c, out);
    crypto_wipe(jk, sizeof jk);
}

void ava1_join_tag(const uint8_t c2s[32], const uint8_t sid[16], uint16_t lane,
                   const uint8_t client_nonce[16], uint8_t out[16]) {
    join_mac(c2s, "join", sid, lane, client_nonce, NULL, out);
}

void ava1_join_ack_tag(const uint8_t s2c[32], const uint8_t sid[16], uint16_t lane,
                       const uint8_t client_nonce[16], const uint8_t server_nonce[16], uint8_t out[16]) {
    join_mac(s2c, "join-ack", sid, lane, client_nonce, server_nonce, out);
}

void ava1_pair_commit(const uint8_t nonce_s[16], uint8_t out[32]) {
    crypto_blake2b(out, 32, nonce_s, 16);
}

void ava1_cpace_generator(const uint8_t h[64], uint32_t code, uint8_t g[32]) {
    static const char label[] = "AVA1 CPace";
    crypto_blake2b_ctx c;
    uint8_t d[32], digits[6];
    int i;
    for (i = 5; i >= 0; i--) {
        digits[i] = (uint8_t)('0' + code % 10u);
        code /= 10u;
    }
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, (const uint8_t *)label, sizeof label - 1);
    crypto_blake2b_update(&c, h, 64);
    crypto_blake2b_update(&c, digits, 6);
    crypto_blake2b_final(&c, d);
    crypto_elligator_map(g, d);
    crypto_wipe(d, sizeof d);
}

static int all_zero32(const uint8_t v[32]) {
    uint8_t acc = 0;
    int i;
    for (i = 0; i < 32; i++) acc |= v[i];
    return acc == 0;
}

int ava1_ct_eq32(const uint8_t a[32], const uint8_t b[32]) { return crypto_verify32(a, b) == 0; }

int ava1_cpace_public(const uint8_t x[32], const uint8_t g[32], uint8_t y[32]) {
    crypto_x25519(y, x, g);
    if (all_zero32(y)) return -1;
    return 0;
}

int ava1_cpace_key(const uint8_t h[64], const uint8_t x[32], const uint8_t y_peer[32], const uint8_t ya[32],
                   const uint8_t yb[32], uint8_t k[32]) {
    static const char label[] = "AVA1 CPace K";
    crypto_blake2b_ctx c;
    uint8_t shared[32];
    crypto_x25519(shared, x, y_peer);
    if (all_zero32(shared)) {
        crypto_wipe(shared, sizeof shared);
        return -1;
    }
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, (const uint8_t *)label, sizeof label - 1);
    crypto_blake2b_update(&c, h, 64);
    crypto_blake2b_update(&c, shared, 32);
    crypto_blake2b_update(&c, ya, 32);
    crypto_blake2b_update(&c, yb, 32);
    crypto_blake2b_final(&c, k);
    crypto_wipe(shared, sizeof shared);
    return 0;
}

void ava1_cpace_mac(const uint8_t k[32], int server, const uint8_t h[64], uint8_t out[32]) {
    crypto_blake2b_ctx c;
    crypto_blake2b_keyed_init(&c, 32, k, 32);
    if (server) crypto_blake2b_update(&c, (const uint8_t *)"server", 6);
    else crypto_blake2b_update(&c, (const uint8_t *)"client", 6);
    crypto_blake2b_update(&c, h, 64);
    crypto_blake2b_final(&c, out);
}

int ava1_random_code(uint32_t *code) {
    uint8_t b[4];
    uint32_t v;
    for (;;) {
        if (ava1_platform_random(b, 4) != 0) return -1;
        v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        if (v < 4294000000u) {
            *code = v % 1000000u;
            crypto_wipe(b, sizeof b);
            return 0;
        }
    }
}

void ava1_launch_proof(const uint8_t token[16], const uint8_t h[64], uint8_t out[16]) {
    static const char label[] = "AVA1 launch";
    crypto_blake2b_ctx c;
    uint8_t key[32], d[32];
    memset(key, 0, sizeof key);
    memcpy(key, token, 16);
    crypto_blake2b_keyed_init(&c, 32, key, 32);
    crypto_blake2b_update(&c, (const uint8_t *)label, sizeof label - 1);
    crypto_blake2b_update(&c, h, 64);
    crypto_blake2b_final(&c, d);
    memcpy(out, d, 16);
    crypto_wipe(key, sizeof key);
    crypto_wipe(d, sizeof d);
}
