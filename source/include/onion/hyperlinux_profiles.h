/* Copyright (C) 2026 HyperHEN contributors */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum OnionHyperLinuxProfile {
  ONION_HYPERLINUX_PROFILE_NONE = 0,
  ONION_HYPERLINUX_PROFILE_STANDARD = 1,
  ONION_HYPERLINUX_PROFILE_1360 = 2,
} OnionHyperLinuxProfile;

/** Select the exact embedded Linux loader profile for a firmware version. */
static inline OnionHyperLinuxProfile
onion_hyperlinux_profile(uint32_t version) {
  switch (version & 0xffff0000U) {
  case 0x03000000U:
  case 0x03100000U:
  case 0x03200000U:
  case 0x03210000U:
  case 0x04000000U:
  case 0x04020000U:
  case 0x04030000U:
  case 0x04500000U:
  case 0x04510000U:
  case 0x05000000U:
  case 0x05020000U:
  case 0x05100000U:
  case 0x05500000U:
  case 0x06000000U:
  case 0x06020000U:
  case 0x06500000U:
  case 0x07000000U:
  case 0x07010000U:
  case 0x07200000U:
  case 0x07400000U:
  case 0x07600000U:
  case 0x07610000U:
  case 0x13200000U:
    return ONION_HYPERLINUX_PROFILE_STANDARD;
  case 0x13600000U:
    return ONION_HYPERLINUX_PROFILE_1360;
  default:
    return ONION_HYPERLINUX_PROFILE_NONE;
  }
}

static inline bool onion_hyperlinux_firmware_supported(uint32_t version) {
  return onion_hyperlinux_profile(version) != ONION_HYPERLINUX_PROFILE_NONE;
}
