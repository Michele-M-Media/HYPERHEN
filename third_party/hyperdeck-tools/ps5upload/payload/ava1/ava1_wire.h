/* AVA1 encoding rules shared by the generated codecs (SPEC.md §3). */
#ifndef AVA1_WIRE_H
#define AVA1_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define AVA1_E_SHORT    (-1)
#define AVA1_E_TRAILING (-2)
#define AVA1_E_UTF8     (-3)
#define AVA1_E_DUP_EXT  (-4)
#define AVA1_E_SPACE    (-5)
#define AVA1_E_MAGIC    (-6)
#define AVA1_E_CRC      (-7)
#define AVA1_E_TOOLONG  (-8)
#define AVA1_E_IO       (-9)
#define AVA1_E_TAG      (-10)
#define AVA1_E_PROTO    (-11)
#define AVA1_E_CLOSED   (-12)
#define AVA1_E_TIMEOUT  (-13)
#define AVA1_E_BUSY     (-14)

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int err;
} ava1_w_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    int err;
} ava1_r_t;

void ava1_w_init(ava1_w_t *w, uint8_t *buf, size_t cap);
void ava1_w_u8(ava1_w_t *w, uint8_t v);
void ava1_w_u16(ava1_w_t *w, uint16_t v);
void ava1_w_u32(ava1_w_t *w, uint32_t v);
void ava1_w_u64(ava1_w_t *w, uint64_t v);
void ava1_w_fixed(ava1_w_t *w, const uint8_t *p, size_t n);
void ava1_w_bytes(ava1_w_t *w, const uint8_t *p, uint32_t n);
void ava1_w_str(ava1_w_t *w, const uint8_t *p, uint16_t n);
/* Writes the tag and a placeholder length; returns where the length sits. */
size_t ava1_w_ext_begin(ava1_w_t *w, uint16_t tag);
void ava1_w_ext_end(ava1_w_t *w, size_t len_at);
/* A u32 length placeholder; ava1_w_len_end patches it with the bytes written since. */
size_t ava1_w_len_begin(ava1_w_t *w);
void ava1_w_len_end(ava1_w_t *w, size_t at);

void ava1_r_init(ava1_r_t *r, const uint8_t *buf, size_t len);
const uint8_t *ava1_r_take(ava1_r_t *r, size_t n);
uint8_t ava1_r_u8(ava1_r_t *r);
uint16_t ava1_r_u16(ava1_r_t *r);
uint32_t ava1_r_u32(ava1_r_t *r);
uint64_t ava1_r_u64(ava1_r_t *r);
void ava1_r_fixed(ava1_r_t *r, uint8_t *out, size_t n);
/* Pointers into the input buffer; valid while it is. */
const uint8_t *ava1_r_bytes(ava1_r_t *r, uint32_t *n);
const uint8_t *ava1_r_str(ava1_r_t *r, uint16_t *n);
/* 0 when no error occurred and every byte was consumed. */
int ava1_r_finish(const ava1_r_t *r);

/* Strict UTF-8 (rejects overlongs, surrogates, > U+10FFFF) — same as Rust's. */
int ava1_utf8_valid(const uint8_t *s, size_t n);

#endif
