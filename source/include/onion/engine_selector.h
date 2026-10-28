/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <onion/boot_mode_5xx.h>
#include <onion/firmware_capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep the established 5.xx wire protocol and per-boot marker paths.
 * Family checks prevent interpreting a choice from a different firmware.
 */
static inline bool onion_engine_selector_is_legacy(uint32_t firmware) {
  return onion_firmware_route(firmware) == ONION_FIRMWARE_ROUTE_LEGACY_HEN;
}

static inline bool onion_engine_selector_supported(uint32_t firmware) {
  const OnionFirmwareRoute route = onion_firmware_route(firmware);
  return route == ONION_FIRMWARE_ROUTE_LEGACY_HEN ||
         route == ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX;
}

/* Presentation key selection; suffix is a string literal. Preserve the
 * established 5.xx translations while using family-correct legacy copy.
 */
#define ONION_ENGINE_SELECTOR_KEY(firmware, suffix) \
  (onion_engine_selector_is_legacy(firmware) \
       ? "notify.selector.legacy_" suffix : "notify.selector." suffix)

static inline bool
onion_engine_selector_hypercore_available(uint32_t firmware) {
  /* The existing HyperCore launcher explicitly rejects firmware < 3.00.
   * Do not remove that guard or infer full support from a ShellCore/PPR
   * table alone. The exact 3.xx/4.xx profiles below have local component
   * profiles; enabling a choice is not hardware certification.
   */
  switch (firmware & 0xffff0000U) {
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
    return true;
  default:
    return false;
  }
}

static inline bool onion_engine_selector_accepts(uint32_t firmware,
                                                  Onion5xxBootMode mode) {
  if (!onion_engine_selector_supported(firmware))
    return false;
  if (mode == ONION_5XX_BOOT_MODE_HYPERCORE)
    return onion_engine_selector_hypercore_available(firmware);
  return onion_engine_selector_is_legacy(firmware)
             ? mode == ONION_5XX_BOOT_MODE_PS5HEN
             : mode == ONION_5XX_BOOT_MODE_BYPERVISOR;
}

/* The selected legacy HyperCore path uses the original A53/PPR service
 * capabilities without changing the default legacy route for other code.
 */
static inline OnionFirmwareRoute
onion_engine_selected_route(uint32_t firmware, Onion5xxBootMode mode) {
  if (onion_engine_selector_is_legacy(firmware) &&
      mode == ONION_5XX_BOOT_MODE_HYPERCORE &&
      onion_engine_selector_accepts(firmware, mode))
    return ONION_FIRMWARE_ROUTE_A53_PPR;
  return onion_firmware_route(firmware);
}

#ifdef __cplusplus
}
#endif
