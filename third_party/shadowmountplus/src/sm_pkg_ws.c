// WebSocket SHA1/Base64 adapted from ps5-pkg-manager ws_upload.c (GPL-3.0).
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include "sm_pkg_ws.h"
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MESSAGE_MAX (SM_PKG_STREAM_CHUNK + 8u)
typedef struct { uint32_t h[5]; uint64_t len; unsigned char buf[64]; size_t buflen; } ws_sha1_t;

static uint32_t rol32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(ws_sha1_t *c, const unsigned char *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 80; i++) w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t fp, k;
        if (i < 20) { fp = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { fp = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { fp = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { fp = b ^ cc ^ d; k = 0xCA62C1D6; }
        uint32_t t = rol32(a, 5) + fp + e + k + w[i];
        e = d; d = cc; cc = rol32(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(ws_sha1_t *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0; c->len = 0; c->buflen = 0;
}

static void sha1_update(ws_sha1_t *c, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    c->len += n;
    while (n > 0) {
        size_t take = 64 - c->buflen;
        if (take > n) take = n;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take; p += take; n -= take;
        if (c->buflen == 64) { sha1_block(c, c->buf); c->buflen = 0; }
    }
}

static void sha1_final(ws_sha1_t *c, unsigned char out[20]) {
    uint64_t bitlen = c->len * 8;
    unsigned char one = 0x80;
    sha1_update(c, &one, 1);
    unsigned char zero = 0;
    while (c->buflen != 56) sha1_update(c, &zero, 1);
    unsigned char lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (unsigned char)(bitlen >> (56 - i * 8));
    /* Append length without affecting c->len accounting further. */
    memcpy(c->buf + 56, lb, 8);
    sha1_block(c, c->buf);
    for (int i = 0; i < 5; i++) {
        out[i*4] = (unsigned char)(c->h[i] >> 24);
        out[i*4+1] = (unsigned char)(c->h[i] >> 16);
        out[i*4+2] = (unsigned char)(c->h[i] >> 8);
        out[i*4+3] = (unsigned char)c->h[i];
    }
}

static int b64_encode(const unsigned char *in, size_t n, char *out, size_t max) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((n + 2) / 3) * 4 + 1;
    if (need > max) return -1;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        if (i + 1 < n) v |= (unsigned)in[i+1] << 8;
        if (i + 2 < n) v |= in[i+2];
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? tab[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? tab[v & 63] : '=';
    }
    out[o] = '\0';
    return 0;
}

static int handshake_accept_key(const char *client_key,
                                   char *out_accept, size_t max) {
    if (!client_key || !client_key[0] || !out_accept || max < 32) return -1;
    /* Trim whitespace (header value). */
    while (*client_key == ' ' || *client_key == '\t') client_key++;
    char key[256];
    snprintf(key, sizeof(key), "%s", client_key);
    size_t klen = strlen(key);
    while (klen > 0 && (key[klen-1] == ' ' || key[klen-1] == '\t' ||
                        key[klen-1] == '\r' || key[klen-1] == '\n'))
        key[--klen] = '\0';
    if (klen == 0) return -1;
    char combo[320];
    snprintf(combo, sizeof(combo), "%s%s", key, WS_GUID);
    ws_sha1_t c;
    sha1_init(&c);
    sha1_update(&c, combo, strlen(combo));
    unsigned char digest[20];
    sha1_final(&c, digest);
    return b64_encode(digest, 20, out_accept, max);
}


int sm_pkg_ws_accept_key(const char *key, char out[32]) {
  if (!key || strlen(key) != 24 || key[22] != '=' || key[23] != '=') return EINVAL;
  for (size_t i = 0; i < 22; ++i)
    if (!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", key[i])) return EINVAL;
  return handshake_accept_key(key, out, 32) == 0 ? 0 : EINVAL;
}

int sm_pkg_ws_decode(unsigned char *data, size_t size, unsigned char *opcode,
                     int *fin, size_t *offset, size_t *length) {
  if (size < 2) return 0;
  unsigned char op = data[0] & 15;
  if ((data[0] & 0x70) || !(data[1] & 0x80) || (op != 0 && op != 1 && op != 2 && op != 8 && op != 9 && op != 10)) return -1;
  uint64_t count = data[1] & 127;
  size_t header = 2;
  if (count == 126) {
    if (size < 4) return 0;
    count = ((uint64_t)data[2] << 8) | data[3]; header = 4;
    if (count < 126) return -1;
  } else if (count == 127) {
    if (size < 10) return 0;
    count = 0;
    for (unsigned i = 2; i < 10; ++i) count = (count << 8) | data[i];
    header = 10;
    if (count <= 65535) return -1;
  }
  bool final = (data[0] & 128) != 0;
  if (count > WS_MESSAGE_MAX || (op >= 8 && (!final || count > 125)) || (op == 8 && count == 1)) return -1;
  if (size < header + 4 + count) return 0;
  unsigned char *mask = data + header, *payload = mask + 4;
  for (size_t i = 0; i < (size_t)count; ++i) payload[i] ^= mask[i & 3];
  *opcode = op; *fin = final; *offset = header + 4; *length = (size_t)count;
  return (int)(header + 4 + count);
}

static bool ws_send_all(int fd, const void *data, size_t size) {
  while (size) {
    ssize_t count = send(fd, data, size, MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    data = (const char *)data + count; size -= (size_t)count;
  }
  return true;
}

static bool send_frame(int fd, unsigned opcode, const void *data, size_t size) {
  unsigned char frame[260];
  if (size > 256) return false;
  frame[0] = (unsigned char)(128 | opcode);
  size_t header = 2;
  if (size < 126) frame[1] = (unsigned char)size;
  else { frame[1] = 126; frame[2] = (unsigned char)(size >> 8); frame[3] = (unsigned char)size; header = 4; }
  if (size) memcpy(frame + header, data, size);
  return ws_send_all(fd, frame, header + size);
}

void sm_pkg_ws_serve(int fd, const char *key, const void *initial, size_t size,
                     sm_pkg_stream_t *stream, const atomic_bool *running) {
  char accept[32], text[256];
  if (sm_pkg_ws_accept_key(key, accept)) return;
  int wake = sm_pkg_stream_wakeup_fd(stream);
  size_t capacity = WS_MESSAGE_MAX + 14;
  if (wake < 0 || size > capacity) { sm_pkg_stream_fail(stream, wake < 0 ? ENOMEM : EPROTO); return; }
  unsigned char *input = malloc(capacity), *message = NULL;
  if (!input) { sm_pkg_stream_fail(stream, ENOMEM); return; }
  int failure = ECONNRESET;
  int length = snprintf(text, sizeof(text), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
  if (length <= 0 || !ws_send_all(fd, text, (size_t)length)) goto end;
  // Older pages ignore max_window and keep their original two-block limit.
  unsigned window = 2;
  length = snprintf(text, sizeof(text), "{\"op\":\"ready\",\"window\":2,\"max_window\":%u}", SM_PKG_STREAM_CLIENTS);
  if (length <= 0 || !send_frame(fd, 1, text, (size_t)length)) goto end;
  if (size) memcpy(input, initial, size);
  size_t used = size, assembled = 0;
  bool fragmented = false;
  uint64_t pending[SM_PKG_STREAM_CLIENTS] = {0};
  unsigned close_code = 1000;
  while (atomic_load(running)) {
    bool refill = false;
    for (unsigned client = 1; client <= window; ++client) {
      if (pending[client - 1]) continue;
      sm_pkg_range_t range;
      int status = sm_pkg_stream_try_request(stream, client, &range);
      if (status == ECANCELED) goto close;
      if (status == EAGAIN) continue;
      if (status) { close_code = 1011; goto close; }
      length = snprintf(text, sizeof(text), "{\"op\":\"range\",\"id\":%llu,\"offset\":%llu,\"size\":%zu}",
          (unsigned long long)range.id, (unsigned long long)range.offset, range.size);
      if (length <= 0 || !send_frame(fd, 1, text, (size_t)length)) goto end;
      pending[client - 1] = range.id;
    }
    for (;;) {
      unsigned char opcode; int fin; size_t offset, count;
      int consumed = sm_pkg_ws_decode(input, used, &opcode, &fin, &offset, &count);
      if (consumed < 0) { close_code = 1002; failure = EPROTO; goto close; }
      if (!consumed) break;
      unsigned char *payload = input + offset;
      if (opcode == 8) goto close;
      if (opcode == 9) { if (!send_frame(fd, 10, payload, count)) goto end; }
      else if (opcode == 1) {
        static const char request[] = "{\"op\":\"window\",\"size\":4}";
        if (!fin || fragmented || count != sizeof(request) - 1 ||
            memcmp(payload, request, sizeof(request) - 1)) {
          close_code = 1002; failure = EPROTO; goto close;
        }
        window = SM_PKG_STREAM_CLIENTS;
        refill = true;
      }
      else if (opcode != 10) {
        if ((opcode == 2 && fragmented) || (opcode == 0 && !fragmented) || count > WS_MESSAGE_MAX - assembled) {
          close_code = 1002; failure = EPROTO; goto close;
        }
        const unsigned char *complete = payload;
        size_t total = count;
        // Ordinary binary messages go directly into the cache. Only fragmented
        // messages need a separate assembly buffer and another full-block copy.
        if (fragmented || !fin) {
          if (!message) message = malloc(WS_MESSAGE_MAX);
          if (!message) { close_code = 1011; failure = ENOMEM; goto close; }
          memcpy(message + assembled, payload, count); assembled += count;
          complete = message; total = assembled;
        }
        fragmented = !fin;
        if (fin) {
          if (total < 8) { close_code = 1002; failure = EPROTO; goto close; }
          uint64_t id = 0;
          for (unsigned i = 0; i < 8; ++i) id |= (uint64_t)complete[i] << (8 * i);
          unsigned slot = 0;
          while (slot < SM_PKG_STREAM_CLIENTS && pending[slot] != id) ++slot;
          if (!id || slot == SM_PKG_STREAM_CLIENTS) { close_code = 1002; failure = EPROTO; goto close; }
          int replied = sm_pkg_stream_reply(stream, id, complete + 8, total - 8);
          if (replied) {
            failure = replied;
            close_code = replied == ECANCELED ? 1000 : 1002;
            goto close;
          }
          pending[slot] = 0; assembled = 0;
          refill = true;
          length = snprintf(text, sizeof(text), "{\"op\":\"ack\",\"id\":%llu}", (unsigned long long)id);
          if (length <= 0 || !send_frame(fd, 1, text, (size_t)length)) goto end;
        }
      }
      memmove(input, input + consumed, used - (size_t)consumed); used -= (size_t)consumed;
    }
    if (refill) continue;
    // A completed upload can have queued more demand while frames were decoded.
    bool available = false;
    for (unsigned i = 0; i < window; ++i)
      if (!pending[i]) available = true;
    if (available && used == 0) {
      struct pollfd ready = {.fd = wake, .events = POLLIN};
      if (poll(&ready, 1, 0) > 0) {
        unsigned char bytes[128]; while (read(wake, bytes, sizeof(bytes)) > 0) {}
        continue;
      }
    }
    struct pollfd events[2] = {{.fd = fd, .events = POLLIN}, {.fd = wake, .events = POLLIN}};
    int rc = poll(events, 2, -1);
    if (rc < 0) { if (errno == EINTR) continue; goto end; }
    if (events[1].revents & POLLIN) { unsigned char bytes[128]; while (read(wake, bytes, sizeof(bytes)) > 0) {} }
    if (events[0].revents & POLLIN) {
      if (used == capacity) { close_code = 1009; failure = EPROTO; goto close; }
      ssize_t got = recv(fd, input + used, capacity - used, 0);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) goto end;
      used += (size_t)got;
    } else if (events[0].revents & (POLLERR | POLLHUP | POLLNVAL)) goto end;
  }
close: {
    unsigned char code[2] = {(unsigned char)(close_code >> 8), (unsigned char)close_code};
    (void)send_frame(fd, 8, code, sizeof(code));
  }
end:
  sm_pkg_stream_fail(stream, failure);
  free(input); free(message);
}
