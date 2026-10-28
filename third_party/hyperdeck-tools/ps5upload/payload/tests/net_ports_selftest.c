/* Host test for net_ports.c: a loopback listener reads as "console", one on every address as
 * "all", a UDP socket as udp, and a free port is not listed. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../src/net_ports.c"

static int listen_on(int type, uint32_t addr, int *port) {
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    int s = socket(AF_INET, type, 0);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = addr;
    if (s < 0 || bind(s, (struct sockaddr *)&sa, sizeof sa) != 0) return -1;
    if (type == SOCK_STREAM && listen(s, 1) != 0) return -1;
    getsockname(s, (struct sockaddr *)&sa, &len);
    *port = ntohs(sa.sin_port);
    return s;
}

static int fails;
static void expect(const char *resp, int port, const char *proto, const char *scope) {
    char want[96];
    snprintf(want, sizeof want, "{\"port\":%d,\"proto\":\"%s\",\"scope\":\"%s\"}", port, proto, scope);
    if (!strstr(resp, want)) {
        printf("FAIL: %s not in %s\n", want, resp);
        fails++;
    }
}

int main(void) {
    char resp[8192];
    const char *body = "{\"lan\":\"127.0.0.2\"}"; /* any non-loopback-127.0.0.1 address */
    int lo_port, any_port, udp_port, lo, any, udp, lo_end;
    size_t n;
    lo = listen_on(SOCK_STREAM, htonl(INADDR_LOOPBACK), &lo_port);
    any = listen_on(SOCK_STREAM, htonl(INADDR_ANY), &any_port);
    udp = listen_on(SOCK_DGRAM, htonl(INADDR_LOOPBACK), &udp_port);
    if (lo < 0 || any < 0 || udp < 0) {
        printf("FAIL: could not open test sockets\n");
        return 1;
    }

    n = net_ports_scan(body, strlen(body), lo_port, lo_port, resp, sizeof resp);
    if (n == 0 || resp[n - 1] != '}') { printf("FAIL: bad answer %s\n", resp); fails++; }
    expect(resp, lo_port, "tcp", "console");

    n = net_ports_scan(body, strlen(body), any_port, any_port, resp, sizeof resp);
    (void)n;
    /* 127.0.0.2 only exists on Linux's loopback; elsewhere the LAN bind fails and the port
     * reads "console" from the loopback check alone. Either is right for a wildcard socket. */
    if (!strstr(resp, "\"scope\":\"all\"") && !strstr(resp, "\"scope\":\"console\"")) {
        printf("FAIL: wildcard listener: %s\n", resp);
        fails++;
    }

    net_ports_scan(body, strlen(body), udp_port, udp_port, resp, sizeof resp);
    expect(resp, udp_port, "udp", "console");

    /* Closed: no longer listed (the UDP socket may share the number, so only tcp counts). */
    close(lo);
    net_ports_scan(body, strlen(body), lo_port, lo_port, resp, sizeof resp);
    if (strstr(resp, "\"tcp\"")) { printf("FAIL: closed port listed: %s\n", resp); fails++; }

    /* No LAN address: held ports read "busy". */
    net_ports_scan("{}", 2, any_port, any_port, resp, sizeof resp);
    expect(resp, any_port, "tcp", "busy");

    /* A tiny buffer truncates instead of overflowing. */
    lo_end = any_port;
    n = net_ports_scan(body, strlen(body), lo_end, lo_end, resp, 130);
    if (n >= 130 || resp[n] != '\0') { printf("FAIL: overflow\n"); fails++; }

    close(any);
    close(udp);
    if (fails) return 1;
    printf("net_ports selftest: ok\n");
    return 0;
}
