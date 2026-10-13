/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/net.h>

#include <ps5/net_ctl.h>

#include <string.h>

static void write_placeholder(char *out, size_t out_size) {
  /*
   * The previous shellui/util copies did
   *   memcpy(out, "IP NOT FOUND", sizeof(info.ip_address))
   * which reads 16 bytes out of a 13-byte string literal. Copy the literal's
   * real length instead.
   */
  const size_t len = sizeof(ONION_NET_IP_UNKNOWN); /* includes the NUL */
  if (out_size < len) {
    out[0] = '\0';
    return;
  }
  memcpy(out, ONION_NET_IP_UNKNOWN, len);
}

onion_net_ip_status onion_net_get_ip_address(char *out, size_t out_size) {
  SceNetCtlInfo info;

  if (out == NULL || out_size < ONION_NET_IP_ADDRESS_SIZE) {
    return ONION_NET_IP_BAD_BUFFER;
  }

  memset(&info, 0, sizeof(info));

  const int32_t ret = sceNetCtlGetInfo(SCE_NET_CTL_INFO_IP_ADDRESS, &info);
  if (ret < 0) {
    write_placeholder(out, out_size);
    switch ((uint32_t)ret) {
    case SCE_NET_CTL_ERROR_NOT_CONNECTED:
      return ONION_NET_IP_DISCONNECTED;
    case SCE_NET_CTL_ERROR_NOT_AVAIL:
      return ONION_NET_IP_UNAVAILABLE;
    default:
      return ONION_NET_IP_ERROR;
    }
  }

  /* sceNetCtlGetInfo does not guarantee termination on a truncated field. */
  memcpy(out, info.ip_address, sizeof(info.ip_address));
  out[sizeof(info.ip_address) - 1] = '\0';
  return ONION_NET_IP_OK;
}

bool onion_net_has_ipv4(void) {
  char ip[ONION_NET_IP_ADDRESS_SIZE];
  return onion_net_get_ip_address(ip, sizeof(ip)) == ONION_NET_IP_OK;
}
