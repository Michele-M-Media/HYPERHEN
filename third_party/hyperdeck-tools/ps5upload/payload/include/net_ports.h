/* The ports in use on the console, behind net.ports / NET_PORTS (Discord ask: "see what
 * payloads are using what ports"). Split out of runtime.c so the host test runs the real code.
 *
 * The kernel's socket tables are not readable here: net.inet.tcp.pcblist, net.inet.udp.pcblist
 * and kern.file all answer ENOENT on FW 13.60. Connecting to every port would find listeners,
 * but a connection that sends nothing can wedge a loader (elfldr did). So the scan is passive:
 * it binds each port and closes at once. EADDRINUSE means a socket already holds it; nothing is
 * accepted, sent or held. Binding the loopback address and the console's LAN address as well
 * tells where it is reachable from.
 *
 * Request: {"lan":"a.b.c.d"} (the address the caller reached the console at; optional).
 * Answer:  {"ok":true,"elapsed_ms":N,"ports":[{"port":P,"proto":"tcp"|"udp","scope":S},...]}
 *   scope "all"     bound to every address: reachable from the network
 *         "console" bound to 127.0.0.1 only: payloads on the console can use it, a PC cannot
 *         "lan"     bound to the LAN address only: most often an outgoing connection
 *         "busy"    held, but the address checks were not possible (no LAN address given)
 * Ports the console refuses to bind at all (EACCES on Sony's reserved ranges) are skipped. */
#ifndef PS5UPLOAD_NET_PORTS_H
#define PS5UPLOAD_NET_PORTS_H

#include <stddef.h>

/* Scans ports first..last (1..65535 clamped) and writes the answer into `resp`. Returns its
 * length; when `cap` runs out the list stops early and the answer says "truncated":true. */
size_t net_ports_scan(const char *body, size_t body_len, int first, int last, char *resp,
                      size_t cap);

#endif
