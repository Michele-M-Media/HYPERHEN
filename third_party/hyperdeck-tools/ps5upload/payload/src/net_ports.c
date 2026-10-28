#include "net_ports.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* The string value of "key":"value" in a flat JSON object; 0 when found. */
static int field(const char *body, size_t len, const char *key, char *out, size_t cap) {
    char needle[32];
    const char *p, *end = body + len, *q;
    size_t n = 0, nl;
    if (snprintf(needle, sizeof needle, "\"%s\"", key) >= (int)sizeof needle) return -1;
    nl = strlen(needle);
    for (p = body; p + nl <= end; p++)
        if (memcmp(p, needle, nl) == 0) break;
    if (p + nl > end) return -1;
    p += nl;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p >= end || *p != ':') return -1;
    p++;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p >= end || *p != '"') return -1;
    for (q = p + 1; q < end && *q != '"'; q++)
        if (n + 1 < cap) out[n++] = *q;
    if (q >= end || cap == 0) return -1;
    out[n] = '\0';
    return 0;
}

/* 1 when something already holds `port` at `addr` (network order), 0 when it is free, -1 when
 * the bind failed for another reason (EACCES on a reserved port, out of descriptors). */
static int held(int type, uint32_t addr, int port) {
    struct sockaddr_in sa;
    int s = socket(AF_INET, type, 0), rc, e;
    if (s < 0) return -1;
    memset(&sa, 0, sizeof sa);
#ifdef __FreeBSD__
    sa.sin_len = sizeof sa;
#endif
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = addr;
    rc = bind(s, (struct sockaddr *)&sa, sizeof sa);
    e = rc ? errno : 0;
    close(s);
    if (rc == 0) return 0;
    return e == EADDRINUSE ? 1 : -1;
}

static const char *scope_of(int type, int port, uint32_t lan) {
    int lo = held(type, htonl(INADDR_LOOPBACK), port), on_lan;
    if (lan == 0) return "busy";
    on_lan = held(type, lan, port);
    if (lo == 1 && on_lan == 1) return "all";
    if (lo == 1) return "console";
    if (on_lan == 1) return "lan";
    return "busy";
}

size_t net_ports_scan(const char *body, size_t body_len, int first, int last, char *resp,
                      size_t cap) {
    char lan_s[64] = {0};
    struct in_addr lan_a;
    uint32_t lan = 0;
    struct timespec t0, t1;
    size_t n = 0;
    int count = 0, truncated = 0, port, t;
    static const int types[2] = {SOCK_STREAM, SOCK_DGRAM};

    if (cap < 128) return 0;
    if (body && field(body, body_len, "lan", lan_s, sizeof lan_s) == 0 &&
        inet_pton(AF_INET, lan_s, &lan_a) == 1 && lan_a.s_addr != htonl(INADDR_LOOPBACK))
        lan = lan_a.s_addr;
    if (first < 1) first = 1;
    if (last > 65535) last = 65535;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    n += (size_t)snprintf(resp + n, cap - n, "{\"ok\":true,\"ports\":[");
    for (port = first; port <= last; port++) {
        for (t = 0; t < 2; t++) {
            /* The wildcard bind conflicts with a socket on any address, so one call finds
             * every holder; the two narrower binds only run for held ports. */
            if (held(types[t], htonl(INADDR_ANY), port) != 1) continue;
            if (n + 96 >= cap) {
                truncated = 1;
                break;
            }
            n += (size_t)snprintf(resp + n, cap - n, "%s{\"port\":%d,\"proto\":\"%s\",\"scope\":\"%s\"}",
                                  count ? "," : "", port, t == 0 ? "tcp" : "udp",
                                  scope_of(types[t], port, lan));
            count++;
        }
        if (truncated) break;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    n += (size_t)snprintf(resp + n, cap - n, "],\"elapsed_ms\":%ld%s}",
                          (long)((t1.tv_sec - t0.tv_sec) * 1000 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000000),
                          truncated ? ",\"truncated\":true" : "");
    return n < cap ? n : cap - 1;
}
