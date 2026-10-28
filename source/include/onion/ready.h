/* Copyright (C) 2025 OnionHEN / LightningMods

Cross-process readiness protocol for HyperHEN services.

Services publish a named marker under /system_tmp/hyperhen/ready/<name>.
Consumers wait with timeout instead of fixed sleep() races.

All runtime markers live under the shared HyperHEN system_tmp namespace.
*/

#pragma once

#include <onion/system_tmp.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_READY_ROOT ONION_SYSTEM_TMP_READY_ROOT

/* Well-known service names (startup orchestration) */
#define ONION_READY_UTIL "util"
#define ONION_READY_KSTUFF_STARTING "kstuff_starting"
#define ONION_READY_KSTUFF "kstuff"
#define ONION_READY_KSTUFF_FAILED "kstuff_failed"
#define ONION_READY_BACKPORK "backpork"
#define ONION_READY_BACKPORK_FAILED "backpork_failed"
#define ONION_READY_AUTOMOUNT "automount"
#define ONION_READY_AUTOMOUNT_FAILED "automount_failed"
#define ONION_READY_HYPERVISOR "hypervisor"
#define ONION_READY_HYPERVISOR_ARMED "hypervisor_armed"
#define ONION_READY_HYPERVISOR_FAILED "hypervisor_failed"
#define ONION_READY_HYPERVISOR_5XX "hypervisor_5xx"
#define ONION_READY_HYPERVISOR_5XX_FAILED "hypervisor_5xx_failed"
#define ONION_READY_HYPERVISOR_940 "hypervisor_940"
#define ONION_READY_HYPERVISOR_940_ARMED "hypervisor_940_armed"
#define ONION_READY_HYPERVISOR_940_FAILED "hypervisor_940_failed"
#define ONION_READY_HYPERCORE_NO_PPR "hypercore_no_ppr"
#define ONION_READY_HYPERCORE_940 "hypercore_940"
#define ONION_READY_HYPERCORE_LITE "hypercore_lite"
#define ONION_READY_LEGACY_HEN "legacy_hen"
#define ONION_READY_LEGACY_HEN_FAILED "legacy_hen_failed"
#define ONION_READY_DAEMON "daemon"
#define ONION_READY_TOOLBOX "toolbox"
#define ONION_READY_HYPERDECK "hyperdeck_http"
#define ONION_READY_HYPERDECK_LISTENER "hyperdeck_listener"
#define ONION_READY_HYPERDECK_FAILED "hyperdeck_failed"

/*
 * Runtime flags (same storage as ready markers; typed names replace ad-hoc
 * /system_tmp flag files).
 *   fps_overlay  — legacy only; cleared on boot (FPS inject removed)
 *   util_booted  — util finished at least one full start (mid-session restart detect)
 */
#define ONION_FLAG_FPS_OVERLAY "fps_overlay"
#define ONION_FLAG_UTIL_BOOTED "util_booted"

/* Publish / clear / query. name is a short token (no path separators). */
bool onion_ready_signal(const char *name);
bool onion_ready_clear(const char *name);
bool onion_ready_is_set(const char *name);

/*
 * Process-instance readiness.
 *
 * A service publishes the PID of the process instance it initialized.  A
 * consumer can then distinguish a still-running initialized instance from a
 * replacement process that reused the same service name.  The marker remains
 * compatible with onion_ready_is_set(); only its contents differ from the plain
 * marker value "1".
 */
bool onion_ready_signal_pid(const char *name, pid_t pid);
bool onion_ready_read_pid(const char *name, pid_t *pid_out);
bool onion_ready_matches_pid(const char *name, pid_t pid);

/*
 * Poll until marker is set or timeout_ms elapses.
 * poll_ms is the sleep between checks (clamped to >= 50).
 * Returns true if ready, false on timeout or invalid name.
 */
bool onion_ready_wait(const char *name, int timeout_ms, int poll_ms);

/* Wait until the marker contains exactly the expected process PID. */
bool onion_ready_wait_pid(const char *name, pid_t pid, int timeout_ms,
                          int poll_ms);

/* Build absolute path for a name into buf (for logging). */
bool onion_ready_path(const char *name, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
