#ifndef SM_HYPERHEN_POLICY_H
#define SM_HYPERHEN_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* These exact existing profiles have no verified ShellCore table entry. Keep
 * their R27 scanner/API path available and explicitly advertise the limitation;
 * never borrow another firmware's addresses or claim on-demand hooks exist. */
static inline bool sm_hyperhen_shellcore_limited_firmware(uint32_t raw_firmware) {
  switch (raw_firmware & 0xffff0000u) {
  case 0x01050000u:
  case 0x01100000u:
  case 0x01110000u:
  case 0x01130000u:
  case 0x04020000u:
    return true;
  default:
    return false;
  }
}

#endif
