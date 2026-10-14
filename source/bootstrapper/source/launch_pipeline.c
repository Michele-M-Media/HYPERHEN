/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "launch_pipeline.h"

#include "bootstrap_notify.h"
#include "kstuff_probe.h"

#include <elfldr_remote.h>
#include <onion/firmware_capabilities.h>
#include <onion/log.h>
#include <onion/hyperlinux_profiles.h>
#include <onion/payload.h>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern uint8_t daemon_start[];
extern const unsigned int daemon_size;
extern uint8_t util_start[];
extern const unsigned int util_size;
extern uint8_t hyperhen_elfldr_start[];
extern const unsigned int hyperhen_elfldr_size;
extern uint8_t kstuff_start[];
extern const unsigned int kstuff_size;
extern uint8_t automount_start[];
extern const unsigned int automount_size;
extern uint8_t hypervisor_start[];
extern const unsigned int hypervisor_size;
extern uint8_t hypervisor_5xx_start[];
extern const unsigned int hypervisor_5xx_size;
extern uint8_t legacy_hen_start[];
extern const unsigned int legacy_hen_size;
extern uint8_t hyperdpi_start[];
extern const unsigned int hyperdpi_size;
extern uint8_t hyperlinux_start[];
extern const unsigned int hyperlinux_size;

int sceKernelGetProcessName(int pid, char *name);

typedef struct LaunchContext {
  uint16_t loader_port;
  bool automount_sentinel_created;
} LaunchContext;

#define HYPERHEN_AUTOMOUNT_SENTINEL "/data/.kstuff_noautomount"
#define HYPERHEN_LINUX_BOOT_MARKER "/data/HyperHEN/boot_linux_next"

int bootstrap_linux_boot_if_armed(uint32_t firmware_version) {
  if (access(HYPERHEN_LINUX_BOOT_MARKER, F_OK) != 0)
    return 0;

  if (!onion_hyperlinux_firmware_supported(firmware_version)) {
    LOG_ERROR("HyperLinux marker rejected for unsupported firmware 0x%08x",
              firmware_version);
    (void)unlink(HYPERHEN_LINUX_BOOT_MARKER);
    bootstrap_notify("notify.linux.unsupported");
    return -1;
  }

  if (!elfldr_remote_available()) {
    LOG_ERROR("HyperLinux clean-boot handoff needs elfldr on port %u",
              ELFLDR_REMOTE_PORT);
    bootstrap_notify("notify.elfldr.need_9021");
    return -1;
  }

  bootstrap_notify("notify.linux.starting");
  LOG_INFO("Launching embedded HyperLinux before HyperCore (%u bytes)",
           hyperlinux_size);
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, hyperlinux_start,
                                   (size_t)hyperlinux_size)) {
    LOG_ERROR("Embedded HyperLinux handoff failed");
    bootstrap_notify("notify.linux.launch_failed");
    return -1;
  }

  if (unlink(HYPERHEN_LINUX_BOOT_MARKER) != 0)
    LOG_WARN("HyperLinux marker cleanup failed: %s", strerror(errno));
  return 1;
}

static bool claim_automount_ownership(LaunchContext *context) {
  if (access(HYPERHEN_AUTOMOUNT_SENTINEL, F_OK) == 0)
    return true;

  int fd = open(HYPERHEN_AUTOMOUNT_SENTINEL,
                O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) {
    if (errno == EEXIST)
      return true;
    LOG_ERROR("Cannot claim HyperHEN automount ownership: %s",
              strerror(errno));
    return false;
  }
  context->automount_sentinel_created = true;
  if (close(fd) != 0)
    LOG_WARN("Automount ownership marker close failed: %s", strerror(errno));
  return true;
}

static void release_automount_ownership(LaunchContext *context) {
  if (!context->automount_sentinel_created)
    return;
  if (onion_ready_is_set(ONION_READY_AUTOMOUNT_FAILED) &&
      access(HYPERHEN_AUTOMOUNT_SENTINEL, F_OK) == 0) {
    LOG_WARN("Keeping automount ownership marker after engine failure");
    context->automount_sentinel_created = false;
    return;
  }
  if (unlink(HYPERHEN_AUTOMOUNT_SENTINEL) != 0 && errno != ENOENT)
    LOG_WARN("Cannot release HyperHEN automount ownership: %s",
             strerror(errno));
  context->automount_sentinel_created = false;
}

static void stop_owned_service(const char *ready_name,
                               const char *expected_process_name) {
  pid_t pid = -1;
  if (!onion_ready_read_pid(ready_name, &pid) || pid <= 1) {
    onion_ready_clear(ready_name);
    return;
  }

  char process_name[32] = {0};
  if (!onion_proc_is_alive(pid) ||
      sceKernelGetProcessName(pid, process_name) != 0 ||
      strcmp(process_name, expected_process_name) != 0) {
    LOG_WARN("Ignoring stale %s ownership marker pid=%d", ready_name,
             (int)pid);
    onion_ready_clear(ready_name);
    return;
  }

  LOG_INFO("Stopping owned %s pid=%d", ready_name, (int)pid);
  if (kill(pid, SIGKILL) != 0 && errno != ESRCH)
    LOG_ERROR("Failed to stop owned %s pid=%d: %s", ready_name, (int)pid,
              strerror(errno));
  onion_ready_clear(ready_name);
}

static bool loader_available(uint16_t port) {
  return port == ONION_ELFLDR_PORT ? elfldr_remote_onion_available()
                                   : elfldr_remote_available_on(port);
}

static bool wait_onion_loader(int timeout_ms) {
  const int poll_ms = 200;
  for (int waited = 0; waited < timeout_ms; waited += poll_ms) {
    if (elfldr_remote_onion_available())
      return true;
    usleep((useconds_t)poll_ms * 1000);
  }
  return false;
}

static bool ensure_private_loader(void) {
  if (elfldr_remote_onion_available()) {
    LOG_WARN("embedded elfldr :9020 already available");
    return true;
  }

  LOG_DEBUG("Starting embedded elfldr on %u via external %u ...",
            ONION_ELFLDR_PORT, ELFLDR_REMOTE_PORT);
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, hyperhen_elfldr_start,
                                   hyperhen_elfldr_size)) {
    LOG_ERROR("  embedded elfldr launch send failed");
    return false;
  }

  if (wait_onion_loader(10000)) {
    LOG_DEBUG("  embedded elfldr :9020 ready");
    return true;
  }

  LOG_DEBUG("  embedded elfldr :9020 did not become ready");
  return false;
}

static bool launch_blob(LaunchContext *context, const uint8_t *elf, size_t size,
                        const char *label, const char *wait_name) {
  if (context->loader_port != ELFLDR_REMOTE_PORT &&
      !loader_available(context->loader_port)) {
    context->loader_port = ELFLDR_REMOTE_PORT;
  }

  LOG_DEBUG("%u memory ELF: %s (%zu bytes)", context->loader_port, label,
            size);
  if (!elfldr_remote_send_bytes_to(context->loader_port, elf, size)) {
    if (context->loader_port != ELFLDR_REMOTE_PORT &&
        elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, elf, size)) {
      LOG_ERROR("  send via %u failed; fallback %u accepted %s",
                context->loader_port, ELFLDR_REMOTE_PORT, label);
      context->loader_port = ELFLDR_REMOTE_PORT;
    } else {
      LOG_ERROR("  send FAILED %s", label);
      return false;
    }
  }

  if (!wait_name || wait_name[0] == '\0') {
    LOG_DEBUG("  sent: %s", label);
    return true;
  }

  for (int i = 0; i < 30; ++i) {
    if (wait_name && onion_find_pid_substr(wait_name) > 0) {
      LOG_DEBUG("  running: %s", wait_name);
      return true;
    }
    sleep(1);
  }
  LOG_DEBUG("  sent %s (process name not seen yet, continuing)", label);
  return true;
}

static bool firmware_uses_hypervisor(uint32_t firmware_version) {
  return onion_firmware_route(firmware_version) ==
         ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX;
}

static bool firmware_uses_hypervisor_5xx(uint32_t firmware_version) {
  return onion_firmware_route(firmware_version) ==
         ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX;
}

static bool firmware_uses_legacy_hen(uint32_t firmware_version) {
  return onion_firmware_route(firmware_version) ==
         ONION_FIRMWARE_ROUTE_LEGACY_HEN;
}

static void prepare_hypervisor_markers(void) {
  if (onion_ready_is_set(ONION_READY_HYPERVISOR))
    return;

  pid_t armed_pid = -1;
  if (onion_ready_read_pid(ONION_READY_HYPERVISOR_ARMED, &armed_pid) &&
      onion_proc_is_alive(armed_pid)) {
    return;
  }

  onion_ready_clear(ONION_READY_HYPERVISOR_ARMED);
  onion_ready_clear(ONION_READY_HYPERVISOR_FAILED);
}

static bool wait_for_hypervisor_ready(void) {
  int missing_process_checks = 0;
  for (;;) {
    if (onion_ready_is_set(ONION_READY_HYPERVISOR))
      return true;
    if (onion_ready_is_set(ONION_READY_HYPERVISOR_FAILED)) {
      bootstrap_notify("HyperHEN Hypervisor failed before completion");
      return false;
    }

    pid_t armed_pid = -1;
    if (onion_ready_read_pid(ONION_READY_HYPERVISOR_ARMED, &armed_pid) &&
        onion_proc_is_alive(armed_pid)) {
      missing_process_checks = 0;
    } else if (++missing_process_checks >= 10) {
      bootstrap_notify("HyperHEN Hypervisor stopped before completion");
      return false;
    }
    sleep(1);
  }
}

static void prepare_hypervisor_5xx_markers(void) {
  onion_ready_clear(ONION_READY_HYPERVISOR_5XX);
  onion_ready_clear(ONION_READY_HYPERVISOR_5XX_FAILED);
}

static void prepare_legacy_hen_markers(void) {
  onion_ready_clear(ONION_READY_LEGACY_HEN);
  onion_ready_clear(ONION_READY_LEGACY_HEN_FAILED);
}

static void prepare_automount_markers(void) {
  stop_owned_service(ONION_READY_AUTOMOUNT, "hyperhen_mount.elf");
  onion_ready_clear(ONION_READY_AUTOMOUNT);
  onion_ready_clear(ONION_READY_AUTOMOUNT_FAILED);
}

static bool wait_for_stage_ready(const char *ready_marker,
                                 const char *failed_marker,
                                 int timeout_ms) {
  const int sleep_ms = 100;
  const int attempts = timeout_ms / sleep_ms;

  for (int attempt = 0; attempt < attempts; ++attempt) {
    if (onion_ready_is_set(ready_marker)) {
      return true;
    }
    if (onion_ready_is_set(failed_marker)) {
      return false;
    }
    usleep((useconds_t)sleep_ms * 1000U);
  }

  return onion_ready_is_set(ready_marker);
}

static int hypercore_ready_timeout_ms(uint32_t firmware_version) {
  /*
   * The exact A53/PPR path performs a mailbox discovery handshake followed by
   * verified DECI5S transactions.  A valid slow retry can exceed the old
   * fixed one-minute window, especially on 10.xx.  Other routes do not run
   * that installer and keep a shorter ceiling.
   */
  return onion_firmware_route(firmware_version) ==
                 ONION_FIRMWARE_ROUTE_A53_PPR
             ? 600000
             : 180000;
}

static bool wait_for_hypercore_ready(uint32_t firmware_version) {
  const int poll_ms = 200;
  const int timeout_ms = hypercore_ready_timeout_ms(firmware_version);
  bool starting_seen = false;
  bool marker_loss_logged = false;

  for (int waited = 0; waited < timeout_ms; waited += poll_ms) {
    /*
     * Preserve the release build's capability probe as the authoritative
     * compatibility fallback.  Older/slow loaders can activate the kernel
     * patches before publishing the newer lifecycle markers; blocking on the
     * marker alone prevents the common Automount/Backport stage from ever
     * being reached even though HyperCore is already usable.
     */
    if ((waited % 1000) == 0 && kstuff_already_running()) {
      onion_ready_clear(ONION_READY_KSTUFF_FAILED);
      if (!onion_ready_signal(ONION_READY_KSTUFF))
        LOG_WARN("HyperCore capability is active but readiness publication "
                 "failed");
      LOG_DEBUG("HyperCore capability probe passed after %d ms", waited);
      return true;
    }

    if (onion_ready_is_set(ONION_READY_KSTUFF_FAILED)) {
      if (kstuff_already_running()) {
        onion_ready_clear(ONION_READY_KSTUFF_FAILED);
        onion_ready_signal(ONION_READY_KSTUFF);
        LOG_WARN("HyperCore failure marker superseded by a successful "
                 "capability probe");
        return true;
      }
      return false;
    }

    pid_t starting_pid = -1;
    if (onion_ready_read_pid(ONION_READY_KSTUFF_STARTING, &starting_pid)) {
      starting_seen = true;
      if (!onion_proc_is_alive(starting_pid)) {
        if (kstuff_already_running()) {
          onion_ready_signal(ONION_READY_KSTUFF);
          return true;
        }
        LOG_ERROR("HyperCore startup process pid=%d exited before readiness",
                  (int)starting_pid);
        return false;
      }
    } else if (starting_seen && !marker_loss_logged) {
      /* Continue with the published mprotect probe instead of suppressing the
       * shared Automount/Backport stage because a transient marker vanished. */
      LOG_WARN("HyperCore startup marker disappeared; continuing capability "
               "probe");
      marker_loss_logged = true;
    }

    usleep((useconds_t)poll_ms * 1000U);
  }

  LOG_ERROR("HyperCore startup exceeded its firmware-route timeout");
  return onion_ready_is_set(ONION_READY_KSTUFF);
}

static bool launch_hypervisor_5xx(LaunchContext *context) {
  prepare_hypervisor_5xx_markers();
  LOG_DEBUG("Loading embedded HyperHEN Byepervisor 5.xx via %u ...",
            context->loader_port);
  if (!launch_blob(context, hypervisor_5xx_start, (size_t)hypervisor_5xx_size,
                   "HyperHEN Byepervisor 5.xx", NULL)) {
    bootstrap_notify("HyperHEN Byepervisor 5.xx could not be launched");
    return false;
  }

  return wait_for_stage_ready(ONION_READY_HYPERVISOR_5XX,
                              ONION_READY_HYPERVISOR_5XX_FAILED, 60000);
}

static bool launch_legacy_hen(LaunchContext *context) {
  prepare_legacy_hen_markers();
  LOG_DEBUG("Loading embedded HyperHEN legacy engine via %u ...",
            context->loader_port);
  if (!launch_blob(context, legacy_hen_start, (size_t)legacy_hen_size,
                   "HyperHEN legacy engine", NULL)) {
    bootstrap_notify("HyperHEN legacy engine could not be launched");
    return false;
  }

  return wait_for_stage_ready(ONION_READY_LEGACY_HEN,
                              ONION_READY_LEGACY_HEN_FAILED, 60000);
}

static bool launch_automount(LaunchContext *context) {
  prepare_automount_markers();
  LOG_DEBUG("Starting embedded HyperHEN Automount via %u ...",
            context->loader_port);
  if (!launch_blob(context, automount_start, (size_t)automount_size,
                   "HyperHEN Automount", NULL)) {
    bootstrap_notify("HyperHEN Automount could not be launched");
    return false;
  }

  if (!wait_for_stage_ready(ONION_READY_AUTOMOUNT,
                            ONION_READY_AUTOMOUNT_FAILED, 180000)) {
    if (!onion_ready_is_set(ONION_READY_AUTOMOUNT_FAILED))
      onion_ready_signal(ONION_READY_AUTOMOUNT_FAILED);
    bootstrap_notify("HyperHEN Automount failed during initialization");
    return false;
  }
  return true;
}

static bool launch_hypervisor(LaunchContext *context,
                              uint32_t firmware_version) {
  if (!firmware_uses_hypervisor(firmware_version))
    return true;

  if (onion_ready_is_set(ONION_READY_HYPERVISOR)) {
    LOG_DEBUG("Hypervisor route already ready");
    return true;
  }

  pid_t armed_pid = -1;
  const bool already_armed =
      onion_ready_read_pid(ONION_READY_HYPERVISOR_ARMED, &armed_pid) &&
      onion_proc_is_alive(armed_pid);
  if (!already_armed) {
    onion_ready_clear(ONION_READY_HYPERVISOR_ARMED);
    onion_ready_clear(ONION_READY_HYPERVISOR_FAILED);
    LOG_DEBUG("Loading embedded HyperHEN Hypervisor via %u ...",
              context->loader_port);
    if (!launch_blob(context, hypervisor_start, (size_t)hypervisor_size,
                     "HyperHEN Hypervisor", NULL)) {
      bootstrap_notify("HyperHEN Hypervisor could not be launched");
      return false;
    }
  } else {
    LOG_DEBUG("Hypervisor route is already armed pid=%d", (int)armed_pid);
  }

  for (int waited = 0; waited < 30; ++waited) {
    if (onion_ready_is_set(ONION_READY_HYPERVISOR) ||
        onion_ready_is_set(ONION_READY_HYPERVISOR_ARMED))
      break;
    if (onion_ready_is_set(ONION_READY_HYPERVISOR_FAILED))
      return false;
    sleep(1);
  }
  if (!onion_ready_is_set(ONION_READY_HYPERVISOR) &&
      !onion_ready_is_set(ONION_READY_HYPERVISOR_ARMED)) {
    bootstrap_notify("HyperHEN Hypervisor did not arm");
    return false;
  }

  return wait_for_hypervisor_ready();
}

static void prepare_service_markers(void) {
  stop_owned_service(ONION_READY_DAEMON, "hyperhen_daemon.elf");
  stop_owned_service(ONION_READY_UTIL, "hyperhen_util.elf");
  prepare_automount_markers();
  onion_ready_clear(ONION_READY_KSTUFF_STARTING);
  onion_ready_clear(ONION_READY_HYPERCORE_NO_PPR);
  prepare_hypervisor_markers();
  onion_ready_clear(ONION_FLAG_UTIL_BOOTED);
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);
}

static bool launch_util(LaunchContext *context) {
  LOG_DEBUG("Starting util via %u ...", context->loader_port);
  if (!launch_blob(context, util_start, util_size, "util", "hyperhen_util.elf")) {
    bootstrap_notify("notify.elfldr.launch_util");
    return false;
  }
  if (!onion_ready_wait(ONION_READY_UTIL, 15000, 200)) {
    LOG_ERROR("util did not publish readiness before timeout");
    bootstrap_notify("HyperHEN utility stopped before initialization completed");
    return false;
  }
  return true;
}

static bool launch_hypercore(LaunchContext *context,
                             uint32_t firmware_version, bool autoload) {
  const bool disabled_by_usb = if_exists("/mnt/usb0/no_kstuff");
  if (disabled_by_usb || !autoload) {
    LOG_DEBUG("kstuff disabled (%s)",
              disabled_by_usb ? "usb no_kstuff" : "kstuff.autoload=false");
    return false;
  }

  if (firmware_version < 0x3000000) {
    return false;
  }

  if (kstuff_already_running()) {
    LOG_WARN("HyperCore capability already active; restoring runtime "
             "readiness and continuing to Automount/Backport");
    onion_ready_clear(ONION_READY_KSTUFF_FAILED);
    onion_ready_signal(ONION_READY_KSTUFF);
    return true;
  }

  onion_ready_clear(ONION_READY_KSTUFF);
  onion_ready_clear(ONION_READY_KSTUFF_FAILED);
  onion_ready_clear(ONION_READY_KSTUFF_STARTING);

  LOG_DEBUG("Loading embedded HyperCore via %u (before daemon/toolbox) ...",
            context->loader_port);
  const bool sent = launch_blob(context, kstuff_start, (size_t)kstuff_size,
                                "HyperCore", NULL);
  if (!sent) {
    bootstrap_notify("notify.kstuff.load_elfldr_failed");
    return false;
  }

  if (!wait_for_hypercore_ready(firmware_version)) {
    bootstrap_notify("HyperCore stopped before completing its ShellCore "
                     "initialization");
    return false;
  }

  if (!kstuff_already_running()) {
    LOG_ERROR("HyperCore published readiness without the mprotect capability");
    onion_ready_clear(ONION_READY_KSTUFF);
    onion_ready_signal(ONION_READY_KSTUFF_FAILED);
    bootstrap_notify("HyperCore readiness verification failed");
    return false;
  }

  LOG_DEBUG("HyperCore readiness marker and mprotect probe verified");
  return true;
}

static bool prepare_hypercore_mode(uint32_t firmware_version) {
  onion_ready_clear(ONION_READY_HYPERCORE_NO_PPR);
  if (onion_firmware_route(firmware_version) !=
      ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR) {
    return true;
  }

  if (!onion_ready_signal(ONION_READY_HYPERCORE_NO_PPR)) {
    LOG_ERROR("Cannot publish HyperCore no-PPR compatibility marker");
    bootstrap_notify("HyperHEN compatibility mode could not be prepared");
    return false;
  }

  LOG_WARN("FW 0x%08x uses HyperCore compatibility mode; PPR features are "
           "disabled",
           firmware_version & 0xffff0000U);
  return true;
}

static bool launch_daemon(LaunchContext *context) {
  LOG_DEBUG("Starting daemon via %u (toolbox inject) ...",
            context->loader_port);
  if (!launch_blob(context, daemon_start, daemon_size, "daemon",
                   "hyperhen_daemon.elf")) {
    bootstrap_notify("notify.elfldr.launch_daemon");
    return false;
  }
  if (!onion_ready_wait(ONION_READY_DAEMON, 20000, 200)) {
    LOG_ERROR("daemon did not publish readiness before timeout");
    bootstrap_notify("HyperHEN daemon stopped before initialization completed");
    return false;
  }
  return true;
}

static bool launch_hyperdpi(LaunchContext *context) {
  LOG_DEBUG("Starting embedded HyperDPI via %u ...", context->loader_port);
  if (!launch_blob(context, hyperdpi_start, (size_t)hyperdpi_size,
                   "HyperDPI", NULL)) {
    bootstrap_notify("HyperDPI could not be launched");
    return false;
  }
  return true;
}

int bootstrap_launch_services(uint32_t firmware_version, bool kstuff_autoload) {
  const OnionFirmwareRoute route = onion_firmware_route(firmware_version);
  const uint32_t capabilities = onion_firmware_capabilities(route);
  const uint32_t mount_stack_capabilities =
      ONION_FIRMWARE_CAP_AUTOMOUNT | ONION_FIRMWARE_CAP_BACKPORT;

  if (route == ONION_FIRMWARE_ROUTE_UNSUPPORTED) {
    LOG_ERROR("No exact HyperHEN route for firmware 0x%08x",
              firmware_version & 0xffff0000U);
    bootstrap_notify("HyperHEN has no exact profile for this firmware");
    return -3;
  }

  if (!elfldr_remote_available()) {
    LOG_DEBUG("FATAL: no elfldr on 127.0.0.1:9021");
    bootstrap_notify("notify.elfldr.need_9021");
    return -2;
  }

  LaunchContext context = {
      .loader_port = ensure_private_loader() ? ONION_ELFLDR_PORT
                                             : ELFLDR_REMOTE_PORT,
      .automount_sentinel_created = false,
  };
  LOG_DEBUG("payload loader port: %u", context.loader_port);
  LOG_DEBUG("launching embedded ELFs (serialized)");
  sleep(3);

  prepare_service_markers();
  if (route == ONION_FIRMWARE_ROUTE_USERLAND_ONLY) {
    if (!launch_util(&context))
      return -2;
    LOG_WARN("FW 0x%08x has no complete HyperCore prerequisite route; "
             "running userland-only",
             firmware_version & 0xffff0000U);
    bootstrap_notify("HyperHEN userland mode active\nHyperCore unavailable "
                     "for this firmware");
    return BOOTSTRAP_LAUNCH_USERLAND_ONLY;
  }

  if ((capabilities & mount_stack_capabilities) !=
      mount_stack_capabilities) {
    LOG_ERROR("Firmware route lacks the required Automount/Backport stack");
    bootstrap_notify("HyperHEN profile is missing Automount/Backport support");
    return -3;
  }

  if (!claim_automount_ownership(&context)) {
    bootstrap_notify("HyperHEN Automount could not claim mount ownership");
    return -2;
  }
  if (firmware_uses_legacy_hen(firmware_version)) {
    if (!launch_legacy_hen(&context)) {
      release_automount_ownership(&context);
      bootstrap_notify("HyperHEN legacy engine failed");
      return -2;
    }
    if (!launch_automount(&context)) {
      release_automount_ownership(&context);
      return -2;
    }
    bootstrap_notify("HyperHEN legacy engine is active");
    return 0;
  }
  if (!launch_util(&context)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (firmware_uses_hypervisor_5xx(firmware_version) &&
      !launch_hypervisor_5xx(&context)) {
    release_automount_ownership(&context);
    bootstrap_notify("HyperHEN Byepervisor 5.xx failed");
    return -2;
  }
  if (!launch_hypervisor(&context, firmware_version)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (!prepare_hypercore_mode(firmware_version)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (!launch_hypercore(&context, firmware_version, kstuff_autoload)) {
    onion_ready_clear(ONION_READY_HYPERCORE_NO_PPR);
    release_automount_ownership(&context);
    bootstrap_notify("HyperHEN Automount requires an active HyperCore");
    return -2;
  }
  if (route == ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR) {
    bootstrap_notify("HyperHEN compatibility mode active\nPPR features are "
                     "disabled");
  }
  if (!launch_automount(&context)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (!launch_daemon(&context))
    return -2;
  if (!launch_hyperdpi(&context))
    LOG_WARN("HyperDPI is unavailable; core HyperHEN services remain active");
  return 0;
}
