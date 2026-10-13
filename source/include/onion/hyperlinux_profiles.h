/* Copyright (C) 2026 HyperHEN contributors */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Exact firmware profiles present in the embedded HyperLinux offset table. */
static inline bool onion_hyperlinux_firmware_supported(uint32_t version) {
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
    return true;
  default:
    return false;
  }
}
