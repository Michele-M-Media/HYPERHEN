#include "net_probe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* The string value of "key":"value" in a flat JSON object; 0 when found. */
static int field(const char *body, size_t len, const char *key, char *out, size_t cap) {
    char needle[32];
    const char *p, *end = body + len, *q;
    size_t n = 0;
    if (snprintf(needle, sizeof needle, "\"%s\"", key) >= (int)sizeof needle) return -1;
    for (p = body; p + strlen(needle) <= end; p++)
        if (memcmp(p, needle, strlen(needle)) == 0) break;
    if (p + strlen(needle) > end) return -1;
    p += strlen(needle);
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

size_t net_probe_reach(const char *body, size_t body_len, char *resp, size_t cap) {
    char host[64] = {0}, port_s[16] = {0}, timeout_s[16] = {0};
    long port, timeout_ms;
    struct sockaddr_in sa;
    struct timespec t0, t1;
    int err_no = 0, timed_out = 0, fd, n;
    long ms;
    if (!body || field(body, body_len, "host", host, sizeof host) != 0 ||
        field(body, body_len, "port", port_s, sizeof port_s) != 0)
        return (size_t)snprintf(resp, cap, "{\"ok\":false,\"err\":\"bad_request\"}");
    (void)field(body, body_len, "timeout_ms", timeout_s, sizeof timeout_s);
    port = strtol(port_s, NULL, 10);
    timeout_ms = timeout_s[0] ? strtol(timeout_s, NULL, 10) : 3000;
    if (timeout_ms < 100) timeout_ms = 100;
    if (timeout_ms > 15000) timeout_ms = 15000;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (port <= 0 || port > 65535 || inet_pton(AF_INET, host, &sa.sin_addr) != 1)
        return (size_t)snprintf(resp, cap, "{\"ok\":false,\"err\":\"bad_address\"}");

    clock_gettime(CLOCK_MONOTONIC, &t0);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err_no = errno;
    } else {
        int fl = fcntl(fd, F_GETFL, 0);
        (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
            if (errno == EINPROGRESS) {
                struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
                int pr = poll(&pfd, 1, (int)timeout_ms);
                if (pr == 0) {
                    timed_out = 1;
                } else if (pr < 0) {
                    err_no = errno;
                } else {
                    int soerr = 0;
                    socklen_t sl = sizeof soerr;
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0) soerr = errno;
                    err_no = soerr;
                }
            } else {
                err_no = errno;
            }
        }
        close(fd);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (long)((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000);
    if (!timed_out && err_no == 0)
        n = snprintf(resp, cap, "{\"ok\":true,\"ms\":%ld}", ms);
    else
        n = snprintf(resp, cap, "{\"ok\":false,\"timed_out\":%s,\"errno\":%d,\"err\":\"%s\",\"ms\":%ld}",
                     timed_out ? "true" : "false", err_no, timed_out ? "timed out" : strerror(err_no), ms);
    return n < 0 ? 0 : ((size_t)n >= cap ? cap - 1 : (size_t)n);
}
