#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Test rollback: the shared 12.70 route and 13.40/13.42 lite stay untouched. */
static inline bool hypercore_13xx_uses_public_v092(uint32_t firmware_version) {
  switch (firmware_version & 0xffff0000U) {
  case 0x13000000U:
  case 0x13200000U:
  case 0x13600000U:
    return true;
  default:
    return false;
  }
}
