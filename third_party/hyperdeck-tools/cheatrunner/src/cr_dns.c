#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "cr_dns.h"

/* Process-lifetime DNS cache. GitHub IPs are stable within a download session, so entries never expire. */
#define DNS_CACHE_MAX 16
typedef struct { char host[128]; char ip[64]; } dns_cache_entry_t;
static dns_cache_entry_t g_dns_cache[DNS_CACHE_MAX];
static int               g_dns_cache_n = 0;
static pthread_mutex_t   g_dns_cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* Skip a DNS name (handles label compression pointers). Returns new offset. */
static size_t
dns_skip_name(const uint8_t *buf, size_t len, size_t pos) {
    while (pos < len) {
        uint8_t b = buf[pos];
        if (b == 0)              return pos + 1;       /* root label */
        if ((b & 0xC0) == 0xC0) return pos + 2;       /* compression pointer */
        pos += 1 + (size_t)b;
    }
    return len;
}

static int
build_query(const char *hostname, uint8_t *buf, size_t buf_sz, uint16_t id) {
    if (buf_sz < 18) return -1;
    size_t pos = 0;

    buf[pos++] = (uint8_t)(id >> 8);
    buf[pos++] = (uint8_t)(id & 0xFF);
    buf[pos++] = 0x01; buf[pos++] = 0x00; /* flags: RD=1 */
    buf[pos++] = 0x00; buf[pos++] = 0x01; /* QDCOUNT=1 */
    buf[pos++] = 0x00; buf[pos++] = 0x00; /* ANCOUNT=0 */
    buf[pos++] = 0x00; buf[pos++] = 0x00; /* NSCOUNT=0 */
    buf[pos++] = 0x00; buf[pos++] = 0x00; /* ARCOUNT=0 */

    /* Encode hostname as DNS labels */
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t label_len = (size_t)(dot - p);
        if (label_len == 0 || label_len > 63 || pos + label_len + 2 >= buf_sz) return -1;
        buf[pos++] = (uint8_t)label_len;
        memcpy(buf + pos, p, label_len);
        pos += label_len;
        p = *dot ? dot + 1 : dot;
    }
    buf[pos++] = 0x00;             /* root label */
    buf[pos++] = 0x00; buf[pos++] = 0x01; /* QTYPE=A */
    buf[pos++] = 0x00; buf[pos++] = 0x01; /* QCLASS=IN */
    return (int)pos;
}

static int
parse_response(const uint8_t *buf, size_t len, uint16_t id, char *ip_out, size_t ip_out_sz) {
    if (len < 12) return -1;
    if ((((uint16_t)buf[0] << 8) | buf[1]) != id) return -1;
    if (!(buf[2] & 0x80)) return -1; /* QR bit not set — not a response */

    uint16_t ancount = (uint16_t)(((uint16_t)buf[6] << 8) | buf[7]);
    if (ancount == 0) return -1;

    /* Skip question section */
    size_t pos = 12;
    pos = dns_skip_name(buf, len, pos);
    pos += 4; /* QTYPE + QCLASS */

    /* Walk answer records */
    for (uint16_t i = 0; i < ancount && pos < len; i++) {
        pos = dns_skip_name(buf, len, pos);
        if (pos + 10 > len) return -1;
        uint16_t type     = (uint16_t)(((uint16_t)buf[pos]   << 8) | buf[pos+1]);
        uint16_t rdlength = (uint16_t)(((uint16_t)buf[pos+8] << 8) | buf[pos+9]);
        pos += 10; /* type(2) + class(2) + ttl(4) + rdlength(2) */
        if (type == 1 && rdlength == 4 && pos + 4 <= len) {
            snprintf(ip_out, ip_out_sz, "%u.%u.%u.%u",
                     buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]);
            return 0;
        }
        pos += rdlength;
    }
    return -1;
}

int
cr_dns_resolve(const char *hostname, char *ip_out, size_t ip_out_sz) {
    if (!hostname || !hostname[0] || !ip_out || ip_out_sz < 8) return -1;

    /* If already a bare IPv4 address, return as-is */
    int is_ip = 1;
    for (const char *p = hostname; *p; p++) {
        if (*p != '.' && (*p < '0' || *p > '9')) { is_ip = 0; break; }
    }
    if (is_ip) { snprintf(ip_out, ip_out_sz, "%s", hostname); return 0; }

    /* Check process-lifetime cache first — avoids a UDP round-trip for repeated lookups */
    pthread_mutex_lock(&g_dns_cache_lock);
    for (int _i = 0; _i < g_dns_cache_n; _i++) {
        if (strcmp(g_dns_cache[_i].host, hostname) == 0) {
            snprintf(ip_out, ip_out_sz, "%s", g_dns_cache[_i].ip);
            pthread_mutex_unlock(&g_dns_cache_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_dns_cache_lock);

    /* Try two public DNS servers — 8.8.8.8 then 1.1.1.1 */
    static const char *dns_servers[] = { "8.8.8.8", "1.1.1.1", NULL };

    uint16_t qid = (uint16_t)arc4random();
    uint8_t query[512], resp[4096];
    int qlen = build_query(hostname, query, sizeof(query), qid);
    if (qlen < 0) return -1;

    for (int si = 0; dns_servers[si]; si++) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) continue;

        struct timeval tv = { 4, 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in dns_addr;
        memset(&dns_addr, 0, sizeof(dns_addr));
        dns_addr.sin_family      = AF_INET;
        dns_addr.sin_port        = htons(53);
        dns_addr.sin_addr.s_addr = inet_addr(dns_servers[si]);

        /* connect() makes the kernel drop replies that do not come from this server/port. */
        if (connect(sock, (struct sockaddr *)&dns_addr, sizeof(dns_addr)) != 0) {
            close(sock);
            continue;
        }

        ssize_t sent = send(sock, query, (size_t)qlen, 0);
        if (sent != qlen) { close(sock); continue; }

        ssize_t n = recv(sock, resp, sizeof(resp), 0);
        close(sock);
        if (n < 12) continue;

        if (parse_response(resp, (size_t)n, qid, ip_out, ip_out_sz) == 0) {
            pthread_mutex_lock(&g_dns_cache_lock);
            if (g_dns_cache_n < DNS_CACHE_MAX) {
                snprintf(g_dns_cache[g_dns_cache_n].host, sizeof(g_dns_cache[0].host), "%s", hostname);
                snprintf(g_dns_cache[g_dns_cache_n].ip,   sizeof(g_dns_cache[0].ip),   "%s", ip_out);
                g_dns_cache_n++;
            }
            pthread_mutex_unlock(&g_dns_cache_lock);
            return 0;
        }
    }
    return -1;
}
