#include "ava1_wire.h"

#include <string.h>

static const uint8_t EMPTY[1] = { 0 };

void ava1_w_init(ava1_w_t *w, uint8_t *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->err = 0;
}

static void put(ava1_w_t *w, const void *p, size_t n) {
    if (w->err || n == 0) return;
    if (n > w->cap - w->len) {
        w->err = AVA1_E_SPACE;
        return;
    }
    memcpy(w->buf + w->len, p, n);
    w->len += n;
}

void ava1_w_u8(ava1_w_t *w, uint8_t v) { put(w, &v, 1); }

void ava1_w_u16(ava1_w_t *w, uint16_t v) {
    uint8_t b[2];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    put(w, b, 2);
}

void ava1_w_u32(ava1_w_t *w, uint32_t v) {
    uint8_t b[4];
    int i;
    for (i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i));
    put(w, b, 4);
}

void ava1_w_u64(ava1_w_t *w, uint64_t v) {
    uint8_t b[8];
    int i;
    for (i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
    put(w, b, 8);
}

void ava1_w_fixed(ava1_w_t *w, const uint8_t *p, size_t n) { put(w, p, n); }

void ava1_w_bytes(ava1_w_t *w, const uint8_t *p, uint32_t n) {
    ava1_w_u32(w, n);
    put(w, p, n);
}

void ava1_w_str(ava1_w_t *w, const uint8_t *p, uint16_t n) {
    ava1_w_u16(w, n);
    put(w, p, n);
}

size_t ava1_w_ext_begin(ava1_w_t *w, uint16_t tag) {
    size_t at;
    ava1_w_u16(w, tag);
    at = w->len;
    ava1_w_u32(w, 0);
    return at;
}

void ava1_w_ext_end(ava1_w_t *w, size_t at) {
    uint32_t n;
    int i;
    if (w->err) return;
    n = (uint32_t)(w->len - at - 4);
    for (i = 0; i < 4; i++) w->buf[at + (size_t)i] = (uint8_t)(n >> (8 * i));
}

size_t ava1_w_len_begin(ava1_w_t *w) {
    size_t at = w->len;
    ava1_w_u32(w, 0);
    return at;
}

void ava1_w_len_end(ava1_w_t *w, size_t at) { ava1_w_ext_end(w, at); }

void ava1_r_init(ava1_r_t *r, const uint8_t *buf, size_t len) {
    r->buf = buf ? buf : EMPTY;
    r->len = buf ? len : 0;
    r->pos = 0;
    r->err = 0;
}

const uint8_t *ava1_r_take(ava1_r_t *r, size_t n) {
    const uint8_t *p;
    if (r->err) return NULL;
    if (n > r->len - r->pos) {
        r->err = AVA1_E_SHORT;
        return NULL;
    }
    p = r->buf + r->pos;
    r->pos += n;
    return p;
}

uint8_t ava1_r_u8(ava1_r_t *r) {
    const uint8_t *p = ava1_r_take(r, 1);
    return p ? p[0] : 0;
}

uint16_t ava1_r_u16(ava1_r_t *r) {
    const uint8_t *p = ava1_r_take(r, 2);
    return p ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
}

uint32_t ava1_r_u32(ava1_r_t *r) {
    const uint8_t *p = ava1_r_take(r, 4);
    return p ? (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)
             : 0;
}

uint64_t ava1_r_u64(ava1_r_t *r) {
    const uint8_t *p = ava1_r_take(r, 8);
    uint64_t v = 0;
    int i;
    if (!p) return 0;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

void ava1_r_fixed(ava1_r_t *r, uint8_t *out, size_t n) {
    const uint8_t *p = ava1_r_take(r, n);
    if (p) memcpy(out, p, n);
}

const uint8_t *ava1_r_bytes(ava1_r_t *r, uint32_t *n) {
    *n = ava1_r_u32(r);
    return ava1_r_take(r, *n);
}

const uint8_t *ava1_r_str(ava1_r_t *r, uint16_t *n) {
    const uint8_t *p;
    *n = ava1_r_u16(r);
    p = ava1_r_take(r, *n);
    if (p && !ava1_utf8_valid(p, *n)) {
        r->err = AVA1_E_UTF8;
        return NULL;
    }
    return p;
}

int ava1_r_finish(const ava1_r_t *r) {
    if (r->err) return r->err;
    return r->pos == r->len ? 0 : AVA1_E_TRAILING;
}

int ava1_utf8_valid(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint8_t lo = 0x80, hi = 0xBF;
        if (c < 0x80) {
            i++;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= n || (s[i + 1] & 0xC0) != 0x80) return 0;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
            if (i + 2 >= n || s[i + 1] < lo || s[i + 1] > hi || (s[i + 2] & 0xC0) != 0x80) return 0;
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
            if (i + 3 >= n || s[i + 1] < lo || s[i + 1] > hi || (s[i + 2] & 0xC0) != 0x80 ||
                (s[i + 3] & 0xC0) != 0x80)
                return 0;
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}
