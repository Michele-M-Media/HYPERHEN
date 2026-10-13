/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * SceNetCtl ABI declarations.
 *
 * The PS5 payload SDK does not ship a netctl header, so every module used to
 * carry its own copy of SceNetCtlInfo (shellui/external_symbols.hpp,
 * util/common_utils.h, daemon/daemon_utils.cpp). The three copies had the same
 * layout but different prototypes for sceNetCtlGetInfo (SceNetCtlInfo* vs
 * void*, int vs int32_t), which is how three subtly different get_ip_address
 * implementations grew. This header is the single source of truth.
 *
 * Declarations only — no HyperHEN behaviour lives here. For reading the
 * console IP use onion_net_get_ip_address() from <onion/net.h>.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_NET_ETHER_ADDR_LEN 6
#define SCE_NET_ETHER_ADDRSTRLEN 18
#define SCE_NET_CTL_SSID_LEN (32 + 1)
#define SCE_NET_CTL_WIFI_SECURITY_KEY_LEN (64 + 1)
#define SCE_NET_CTL_AUTH_NAME_LEN (127 + 1)
#define SCE_NET_CTL_AUTH_KEY_LEN (127 + 1)
#define SCE_NET_CTL_HOSTNAME_LEN (255 + 1)
#define SCE_NET_CTL_IPV4_ADDR_STR_LEN (16)

/* `code` argument to sceNetCtlGetInfo for the IPv4 address field. */
#define SCE_NET_CTL_INFO_IP_ADDRESS 14

/*
 * Returned as a positive 0x8041xxxx bit pattern, so compare against the
 * unsigned value — comparing a signed int result never matches.
 */
#define SCE_NET_CTL_ERROR_NOT_CONNECTED 0x80412108u
#define SCE_NET_CTL_ERROR_NOT_AVAIL 0x80412109u

typedef struct SceNetEtherAddr {
  uint8_t data[SCE_NET_ETHER_ADDR_LEN];
} SceNetEtherAddr;

typedef void (*SceNetCtlCallback)(int eventType, void *arg);

typedef union SceNetCtlInfo {
  uint32_t device;
  SceNetEtherAddr ether_addr;
  uint32_t mtu;
  uint32_t link;
  SceNetEtherAddr bssid;
  char ssid[SCE_NET_CTL_SSID_LEN];
  uint32_t wifi_security;
  int32_t rssi_dbm;
  uint8_t rssi_percentage;
  uint8_t channel;
  uint32_t ip_config;
  char dhcp_hostname[SCE_NET_CTL_HOSTNAME_LEN];
  char pppoe_auth_name[SCE_NET_CTL_AUTH_NAME_LEN];
  char ip_address[SCE_NET_CTL_IPV4_ADDR_STR_LEN];
  char netmask[SCE_NET_CTL_IPV4_ADDR_STR_LEN];
  char default_route[SCE_NET_CTL_IPV4_ADDR_STR_LEN];
  char primary_dns[SCE_NET_CTL_IPV4_ADDR_STR_LEN];
  char secondary_dns[SCE_NET_CTL_IPV4_ADDR_STR_LEN];
  uint32_t http_proxy_config;
  char http_proxy_server[SCE_NET_CTL_HOSTNAME_LEN];
  uint16_t http_proxy_port;
} SceNetCtlInfo;

int32_t sceNetCtlGetInfo(int32_t code, SceNetCtlInfo *info);
void sceNetCtlTerm(void);

#ifdef __cplusplus
}
#endif
