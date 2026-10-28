/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <onion/boot_mode_5xx.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional, fail-open HTTP UI, started only after a successful core pipeline. */
void bootstrap_hyperdeck_start(uint32_t firmware);
/* Mandatory per-boot selector for the exact legacy and 5.xx profiles.
 * The historical function name remains an internal compatibility interface.
 */
Onion5xxBootMode bootstrap_hyperdeck_select_5xx_mode(uint32_t firmware);
/* Legacy HEN requires the browser to be left manually before its engine starts.
 * Retire only the owned selector backend after an observed quiet window.
 */
bool bootstrap_hyperdeck_wait_legacy_browser(uint32_t firmware);
/* Post-pipeline helpers; the parent owns Media installation and diagnostics. */
void bootstrap_hyperdeck_record(const char *stage, int result);
pid_t bootstrap_hyperdeck_spawn(const uint8_t *elf, size_t size);
bool bootstrap_hyperdeck_http_probe(void);
bool bootstrap_hyperdeck_http_probe_selector(void);
int bootstrap_hyperdeck_install_media(void);
#ifdef __cplusplus
}
#endif
