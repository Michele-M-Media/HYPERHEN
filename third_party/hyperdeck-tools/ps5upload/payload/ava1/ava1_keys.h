/* Identities and the AVA1 derivations (SPEC.md §4). Mirrors engine/crates/ava1/src/keys.rs. */
#ifndef AVA1_KEYS_H
#define AVA1_KEYS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t secret[32];
    uint8_t pub[32];
} ava1_identity_t;

void ava1_identity_from_secret(ava1_identity_t *id, const uint8_t secret[32]);
/* 0, or -1 when the shared secret is all-zero (low-order peer key). */
int ava1_dh(const ava1_identity_t *id, const uint8_t peer[32], uint8_t out[32]);
/* keyed BLAKE2b-256(dir, "AVA1 lane" ‖ u16le(lane) ‖ client_nonce ‖ server_nonce): fresh
 * nonces per join, so a re-joined lane never reuses a key (SPEC.md §4.3). */
void ava1_lane_key(const uint8_t dir[32], uint16_t lane, const uint8_t client_nonce[16],
                   const uint8_t server_nonce[16], uint8_t out[32]);
/* The control connection: ava1_lane_key(dir, 0, zeros, zeros). */
void ava1_control_key(const uint8_t dir[32], uint8_t out[32]);
/* Join proof (c2s): "join" ‖ sid ‖ u16le(lane) ‖ client_nonce (SPEC.md §4.5). */
void ava1_join_tag(const uint8_t c2s[32], const uint8_t sid[16], uint16_t lane,
                   const uint8_t client_nonce[16], uint8_t out[16]);
/* JoinAck proof (s2c): "join-ack" ‖ sid ‖ u16le(lane) ‖ client_nonce ‖ server_nonce. */
void ava1_join_ack_tag(const uint8_t s2c[32], const uint8_t sid[16], uint16_t lane,
                       const uint8_t client_nonce[16], const uint8_t server_nonce[16], uint8_t out[16]);
/* The server's commitment to its pairing nonce: BLAKE2b-256(nonce_s) (SPEC.md 4.6). */
void ava1_pair_commit(const uint8_t nonce_s[16], uint8_t out[32]);
/* Pairing PAKE (CPace-style, SPEC.md 4.6 / 5.5). The code is a random six-digit number
 * the console shows on its screen; it is the PAKE password, never derived or sent.
 * G = Elligator2(BLAKE2b-256("AVA1 CPace" ‖ h ‖ six ASCII digits)). */
void ava1_cpace_generator(const uint8_t h[64], uint32_t code, uint8_t g[32]);
/* y = x * G. Returns -1 (and zeroes y) when the result is the all-zero point. */
int ava1_cpace_public(const uint8_t x[32], const uint8_t g[32], uint8_t y[32]);
/* K = BLAKE2b-256("AVA1 CPace K" ‖ h ‖ X25519(x, y_peer) ‖ ya ‖ yb). Returns -1 when the
 * shared secret is all zero (a low-order peer value). */
int ava1_cpace_key(const uint8_t h[64], const uint8_t x[32], const uint8_t y_peer[32], const uint8_t ya[32],
                   const uint8_t yb[32], uint8_t k[32]);
/* Key confirmation: keyed BLAKE2b-256(key = K, "client"|"server" ‖ h). */
void ava1_cpace_mac(const uint8_t k[32], int server, const uint8_t h[64], uint8_t out[32]);
/* Constant-time equality of two 32-byte values. */
int ava1_ct_eq32(const uint8_t a[32], const uint8_t b[32]);
/* A uniformly random six-digit code from the platform CSPRNG; 0 and *code = 0 on success,
 * -1 when no random source. */
int ava1_random_code(uint32_t *code);
/* Launch proof (SPEC.md §5.2): the first 16 bytes of keyed BLAKE2b-256(key = token ‖ 16
 * zero bytes, "AVA1 launch" ‖ h). */
void ava1_launch_proof(const uint8_t token[16], const uint8_t h[64], uint8_t out[16]);

#endif
