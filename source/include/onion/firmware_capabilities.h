/* Copyright (C) 2026 HyperHEN contributors */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One authoritative, fail-closed description of the boot path available for
 * each exact firmware profile.  A component table by itself is not enough to
 * make a firmware bootable: the selected route must also provide every
 * prerequisite needed before that component is started.
 */
typedef enum OnionFirmwareRoute {
  ONION_FIRMWARE_ROUTE_UNSUPPORTED = 0,
  ONION_FIRMWARE_ROUTE_LEGACY_HEN,
  ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX,
  ONION_FIRMWARE_ROUTE_A53_PPR,
  ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR,
  ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX,
  ONION_FIRMWARE_ROUTE_KSTUFF_LITE,
  ONION_FIRMWARE_ROUTE_USERLAND_ONLY,
  ONION_FIRMWARE_ROUTE_HYPERVISOR_940,
} OnionFirmwareRoute;

enum OnionFirmwareCapability {
  ONION_FIRMWARE_CAP_UTIL = 1U << 0,
  ONION_FIRMWARE_CAP_LEGACY_HEN = 1U << 1,
  ONION_FIRMWARE_CAP_HYPERVISOR = 1U << 2,
  ONION_FIRMWARE_CAP_HYPERCORE = 1U << 3,
  ONION_FIRMWARE_CAP_PPR = 1U << 4,
  ONION_FIRMWARE_CAP_AUTOMOUNT = 1U << 5,
  ONION_FIRMWARE_CAP_DAEMON = 1U << 6,
  ONION_FIRMWARE_CAP_TOOLBOX = 1U << 7,
  ONION_FIRMWARE_CAP_HYPERDPI = 1U << 8,
  ONION_FIRMWARE_CAP_BACKPORT = 1U << 9,
};

static inline OnionFirmwareRoute
onion_firmware_route(uint32_t firmware_version) {
  switch (firmware_version & 0xffff0000U) {
  case 0x01000000U:
  case 0x01010000U:
  case 0x01020000U:
  case 0x01050000U:
  case 0x01100000U:
  case 0x01110000U:
  case 0x01120000U:
  case 0x01130000U:
  case 0x01140000U:
  case 0x02000000U:
  case 0x02200000U:
  case 0x02250000U:
  case 0x02260000U:
  case 0x02300000U:
  case 0x02500000U:
  case 0x02700000U:
  case 0x03000000U:
  case 0x03100000U:
  case 0x03200000U:
  case 0x03210000U:
  case 0x04000000U:
  case 0x04020000U:
  case 0x04030000U:
  case 0x04500000U:
  case 0x04510000U:
    return ONION_FIRMWARE_ROUTE_LEGACY_HEN;

  case 0x05000000U:
  case 0x05020000U:
  case 0x05100000U:
  case 0x05500000U:
    return ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX;

  case 0x06000000U:
  case 0x06020000U:
  case 0x06500000U:
  case 0x07000000U:
  case 0x07010000U:
  case 0x07200000U:
  case 0x07400000U:
  case 0x07600000U:
  case 0x07610000U:
  case 0x08000000U:
  case 0x08200000U:
  case 0x08400000U:
  case 0x08600000U:
  case 0x09000000U:
  case 0x09050000U:
  case 0x09200000U:
  case 0x09600000U:
  case 0x10000000U:
  case 0x10010000U:
  case 0x10200000U:
  case 0x10400000U:
  case 0x10600000U:
  case 0x11000000U:
  case 0x11200000U:
  case 0x11400000U:
    return ONION_FIRMWARE_ROUTE_A53_PPR;

  /* Dedicated binary stage; completion is verified after S3 resume. */
  case 0x09400000U:
    return ONION_FIRMWARE_ROUTE_HYPERVISOR_940;

  /*
   * These releases have exact HyperCore and ShellCore tables, but no exact
   * installer profile in the vendored PPR snapshot.  They run HyperCore in a
   * deliberately reduced mode; PPR-dependent behavior stays disabled.
   */
  case 0x11600000U:
  case 0x12000000U:
  case 0x12020000U:
  case 0x12200000U:
  case 0x12400000U:
  case 0x12600000U:
    return ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR;

  case 0x12700000U:
  case 0x13000000U:
  case 0x13200000U:
  case 0x13600000U:
    return ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX;

  /* Exact upstream kstuff-lite tables exist; no Hypervisor/PPR is required. */
  case 0x13400000U:
  case 0x13420000U:
    return ONION_FIRMWARE_ROUTE_KSTUFF_LITE;

  default:
    return ONION_FIRMWARE_ROUTE_UNSUPPORTED;
  }
}

static inline uint32_t
onion_firmware_capabilities(OnionFirmwareRoute route) {
  const uint32_t modern = ONION_FIRMWARE_CAP_UTIL |
                          ONION_FIRMWARE_CAP_HYPERCORE |
                          ONION_FIRMWARE_CAP_AUTOMOUNT |
                          ONION_FIRMWARE_CAP_BACKPORT |
                          ONION_FIRMWARE_CAP_DAEMON |
                          ONION_FIRMWARE_CAP_TOOLBOX |
                          ONION_FIRMWARE_CAP_HYPERDPI;

  switch (route) {
  case ONION_FIRMWARE_ROUTE_LEGACY_HEN:
    return ONION_FIRMWARE_CAP_UTIL |
           ONION_FIRMWARE_CAP_LEGACY_HEN |
           ONION_FIRMWARE_CAP_AUTOMOUNT |
           ONION_FIRMWARE_CAP_BACKPORT |
           ONION_FIRMWARE_CAP_DAEMON |
           ONION_FIRMWARE_CAP_TOOLBOX;
  case ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX:
  case ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX:
    return modern | ONION_FIRMWARE_CAP_HYPERVISOR;
  case ONION_FIRMWARE_ROUTE_HYPERVISOR_940:
    return modern | ONION_FIRMWARE_CAP_HYPERVISOR;
  case ONION_FIRMWARE_ROUTE_A53_PPR:
    return modern | ONION_FIRMWARE_CAP_PPR;
  case ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR:
    return modern;
  case ONION_FIRMWARE_ROUTE_KSTUFF_LITE:
    return ONION_FIRMWARE_CAP_HYPERCORE |
           ONION_FIRMWARE_CAP_AUTOMOUNT |
           ONION_FIRMWARE_CAP_DAEMON |
           ONION_FIRMWARE_CAP_TOOLBOX;
  case ONION_FIRMWARE_ROUTE_USERLAND_ONLY:
    return ONION_FIRMWARE_CAP_UTIL;
  case ONION_FIRMWARE_ROUTE_UNSUPPORTED:
  default:
    return 0;
  }
}

static inline bool onion_firmware_has_capability(uint32_t firmware_version,
                                                  uint32_t capability) {
  return (onion_firmware_capabilities(
              onion_firmware_route(firmware_version)) &
          capability) == capability;
}

/*
 * Compatibility releases may retain the hardware-tested common HyperCore
 * image. These exact routes need the separately source-built image because
 * their prerequisite dispatch is newer than that retained binary.
 */
static inline bool
onion_firmware_uses_dedicated_hypercore(uint32_t firmware_version) {
  const uint32_t exact = firmware_version & 0xffff0000U;
  return exact == 0x09400000U || exact == 0x12700000U;
}

#ifdef __cplusplus
}
#endif
