#pragma once
#include <stddef.h>

/* Resolves a hostname to an IPv4 string by querying 8.8.8.8:53 over raw UDP, bypassing the system DNS
 * (the PS5 DNS may be 127.0.0.1). Returns 0 on success, -1 on failure. */
int cr_dns_resolve(const char *hostname, char *ip_out, size_t ip_out_sz);
