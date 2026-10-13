/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared network helpers (console IPv4 address).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Minimum size of the output buffer accepted by onion_net_get_ip_address().
 * Matches SCE_NET_CTL_IPV4_ADDR_STR_LEN and leaves room for the
 * ONION_NET_IP_UNKNOWN placeholder.
 */
#define ONION_NET_IP_ADDRESS_SIZE 16

/** Placeholder written when the address cannot be read. */
#define ONION_NET_IP_UNKNOWN "IP NOT FOUND"

typedef enum {
  ONION_NET_IP_OK = 0,
  /** Cable unplugged / no association — the caller may want to retry. */
  ONION_NET_IP_DISCONNECTED,
  /** Net stack not available yet (early boot, rest mode resume). */
  ONION_NET_IP_UNAVAILABLE,
  /** sceNetCtlGetInfo failed for any other reason. */
  ONION_NET_IP_ERROR,
  /** out is NULL or out_size < ONION_NET_IP_ADDRESS_SIZE. */
  ONION_NET_IP_BAD_BUFFER,
} onion_net_ip_status;

/**
 * Read the console IPv4 address as a NUL-terminated string.
 *
 * On every status other than ONION_NET_IP_BAD_BUFFER, `out` is written: either
 * the address or the ONION_NET_IP_UNKNOWN placeholder. Callers that only
 * display the value can therefore ignore the return value, which is what the
 * daemon startup banner and the ShellUI overlay do.
 *
 * @param out       buffer receiving the address; must be non-NULL
 * @param out_size  size of `out`; must be >= ONION_NET_IP_ADDRESS_SIZE
 */
onion_net_ip_status onion_net_get_ip_address(char *out, size_t out_size);

/**
 * True when the console currently has an IPv4 address (cable/Wi-Fi associated).
 * This is the local link check used before starting a download; it does not
 * probe the internet or a specific git host.
 */
bool onion_net_has_ipv4(void);

#ifdef __cplusplus
}
#endif
