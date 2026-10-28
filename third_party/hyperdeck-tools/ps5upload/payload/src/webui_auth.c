#include "webui_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void webui_token_new(char out[65]) {
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[32];
    int i;
    arc4random_buf(raw, sizeof raw);
    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 15];
    }
    out[64] = '\0';
}

static char lc(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
}

/* Case-insensitive ASCII equality (no locale). */
static int ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (lc(*a) != lc(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Does "host" (a Host header value) equal "<name>:<port>"? */
static int host_is(const char *host, const char *name, uint16_t port) {
    char want[160];
    int n = snprintf(want, sizeof want, "%s:%u", name, (unsigned)port);
    if (n < 0 || (size_t)n >= sizeof want) return 0;
    return ieq(host, want);
}

static int host_allowed(const char *host, const webui_auth_cfg_t *c) {
    size_t i;
    if (!host[0]) return 0;
    if (host_is(host, "127.0.0.1", c->port) || host_is(host, "localhost", c->port))
        return 1;
    if (c->lan) {
        for (i = 0; i < c->n_own; i++)
            if (c->own_addrs[i] && host_is(host, c->own_addrs[i], c->port)) return 1;
    }
    return 0;
}

static int origin_allowed(const char *origin, const webui_auth_cfg_t *c) {
    static const char pre[] = "http://";
    if (strncmp(origin, pre, sizeof pre - 1) != 0) return 0;
    return host_allowed(origin + sizeof pre - 1, c);
}

/* Constant time over the full 64 chars; false on any length mismatch. */
static int token_ok(const char *got, const char *want) {
    unsigned char diff = 0;
    size_t i;
    if (!want || strlen(want) != 64 || strlen(got) != 64) return 0;
    for (i = 0; i < 64; i++) diff |= (unsigned char)(got[i] ^ want[i]);
    return diff == 0;
}

enum webui_deny webui_check(const webui_req_t *r, const webui_auth_cfg_t *c,
                            int is_api) {
    if (!host_allowed(r->host, c)) return WEBUI_DENY_HOST;
    if (r->origin[0] && !origin_allowed(r->origin, c)) return WEBUI_DENY_ORIGIN;
    if (is_api && !token_ok(r->token, c->token)) return WEBUI_DENY_TOKEN;
    return WEBUI_ALLOW;
}
