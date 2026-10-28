#include "ava1_trust.h"

/* [0..9) magic, [9] state (0 empty, 1 key, 2 key + launch token), [10..16) zero,
 * [16..48) key, [48..64) the launch token in state 2, zero otherwise.
 * Spelled as a character list so the magic exists in exactly one place in the binary
 * (the engine refuses an ELF with two). Never write the magic as a string anywhere
 * else in the payload. volatile + used: the bytes stay in .data and are read at runtime,
 * not folded at compile time. */
__attribute__((used, aligned(16))) volatile uint8_t ava1_trust_slot[64] = {
    'A', 'V', 'A', '1', 'T', 'R', 'U', 'S', 'T',
};

int ava1_trust_slot_key(uint8_t out[32]) {
    int i;
    if (ava1_trust_slot[9] != 1 && ava1_trust_slot[9] != 2) return -1;
    for (i = 0; i < 32; i++) out[i] = ava1_trust_slot[16 + i];
    return 0;
}

int ava1_trust_slot_token(uint8_t out[16]) {
    int i;
    if (ava1_trust_slot[9] != 2) return -1;
    for (i = 0; i < 16; i++) out[i] = ava1_trust_slot[48 + i];
    return 0;
}
