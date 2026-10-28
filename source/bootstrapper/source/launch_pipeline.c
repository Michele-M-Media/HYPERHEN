/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "launch_pipeline.h"

#include "bootstrap_notify.h"
#include "kstuff_probe.h"
#include "automount_preflight.h"
#include "automount_route_proof.h"

#include <elfldr_remote.h>
#include <onion/firmware_capabilities.h>
#include <onion/engine_selector.h>
#include <onion/hypervisor_940_report.h>
#include <onion/log.h>
#include <onion/hyperlinux_profiles.h>
#include <onion/payload.h>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/version.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

extern uint8_t daemon_start[];
extern const unsigned int daemon_size;
extern uint8_t util_start[];
extern const unsigned int util_size;
extern uint8_t hyperhen_elfldr_start[];
extern const unsigned int hyperhen_elfldr_size;
extern uint8_t kstuff_start[];
extern const unsigned int kstuff_size;
extern uint8_t kstuff_lite_start[];
extern const unsigned int kstuff_lite_size;
extern uint8_t kstuff_940_start[];
extern const unsigned int kstuff_940_size;
extern uint8_t kstuff_1270_start[];
extern const unsigned int kstuff_1270_size;
extern uint8_t automount_start[];
extern const unsigned int automount_size;
extern uint8_t automount_lite_start[];
extern const unsigned int automount_lite_size;
extern uint8_t hypervisor_start[];
extern const unsigned int hypervisor_size;
extern uint8_t hypervisor_1270_start[];
extern const unsigned int hypervisor_1270_size;
extern uint8_t hypervisor_5xx_start[];
extern const unsigned int hypervisor_5xx_size;
extern uint8_t hypervisor_940_start[];
extern const unsigned int hypervisor_940_size;
extern uint8_t legacy_hen_start[];
extern const unsigned int legacy_hen_size;
extern uint8_t hyperdpi_start[];
extern const unsigned int hyperdpi_size;
extern uint8_t hyperlinux_start[];
extern const unsigned int hyperlinux_size;
extern uint8_t hyperlinux_1360_start[];
extern const unsigned int hyperlinux_1360_size;

int sceKernelGetProcessName(int pid, char *name);

typedef struct LaunchContext {
  uint16_t loader_port;
  bool automount_sentinel_created;
} LaunchContext;

#define HYPERHEN_AUTOMOUNT_SENTINEL "/data/.kstuff_noautomount"
#define HYPERHEN_LINUX_BOOT_MARKER "/data/HyperHEN/boot_linux_next"
#define HYPERHEN_HYPERVISOR_940_RESULT "/data/phu_hv_result.txt"
#define HYPERHEN_HYPERVISOR_940_RESULT_MAX (1024U * 1024U)
#define HYPERHEN_HYPERVISOR_5XX_STAGE                                    \
  ONION_SYSTEM_TMP_ROOT "/bypervisor5XX.elf"
#define HYPERHEN_HYPERVISOR_5XX_EXIT_TIMEOUT_MS 60000

static int automount_process_snapshot(void *opaque, bool by_pid, pid_t pid,
                                      void **records, size_t *bytes) {
  (void)opaque;
  *records = NULL;
  *bytes = 0;
  int mib[4] = {CTL_KERN, KERN_PROC,
                by_pid ? KERN_PROC_PID : KERN_PROC_PROC,
                by_pid ? (int)pid : 0};
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    size_t needed = 0;
    if (sysctl(mib, 4, NULL, &needed, NULL, 0) != 0) {
      /* A process can exit between the allproc and per-PID query. */
      if (by_pid && errno == ESRCH) return 0;
      return -1;
    }
    if (needed == 0) return 0;
    if (needed > HYPERHEN_SMP_MAX_SNAPSHOT_BYTES) return -1;
    void *buffer = malloc(needed);
    if (!buffer) return -1;
    size_t received = needed;
    if (sysctl(mib, 4, buffer, &received, NULL, 0) == 0) {
      if (received > needed) {
        free(buffer);
        return -1;
      }
      *records = buffer;
      *bytes = received;
      return 0;
    }
    const int saved_errno = errno;
    free(buffer);
    if (by_pid && saved_errno == ESRCH) return 0;
    if (saved_errno != ENOMEM) return -1;
  }
  return -1;
}

static void automount_process_snapshot_release(void *opaque, void *records) {
  (void)opaque;
  free(records);
}

static bool external_automount_preflight(void) {
  const struct HyperhenMountPreflightOps ops = {
      .snapshot = automount_process_snapshot,
      .release = automount_process_snapshot_release,
      .opaque = NULL,
  };
  pid_t pid = 0;
  const int result = hyperhen_mount_preflight(&ops, &pid);
  if (result == HYPERHEN_MOUNT_PREFLIGHT_CLEAR) return true;
  if (result == HYPERHEN_MOUNT_PREFLIGHT_CONFLICT) {
    LOG_ERROR("External ShadowMountPlus pid=%d is active; a clean boot is required",
              (int)pid);
    bootstrap_notify("notify.boot.conflict", "ShadowMountPlus");
    bootstrap_notify("notify.boot.component_restart", "HyperHEN");
  } else {
    LOG_ERROR("Cannot verify external ShadowMountPlus process ownership; startup stopped before dispatch");
    bootstrap_notify("notify.boot.component_readiness_failed", "Automount");
  }
  return false;
}

int bootstrap_linux_boot_if_armed(uint32_t firmware_version) {
  if (access(HYPERHEN_LINUX_BOOT_MARKER, F_OK) != 0)
    return 0;

  const OnionHyperLinuxProfile profile =
      onion_hyperlinux_profile(firmware_version);
  if (profile == ONION_HYPERLINUX_PROFILE_NONE) {
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

  const uint8_t *loader = hyperlinux_start;
  size_t loader_size = (size_t)hyperlinux_size;
  const char *loader_name = "standard";
  if (profile == ONION_HYPERLINUX_PROFILE_1360) {
    loader = hyperlinux_1360_start;
    loader_size = (size_t)hyperlinux_1360_size;
    loader_name = "13.60 dedicated";
  }
  if (loader_size == 0) {
    LOG_ERROR("HyperLinux %s loader is empty", loader_name);
    bootstrap_notify("notify.linux.launch_failed");
    return -1;
  }

  bootstrap_notify("notify.linux.starting");
  LOG_INFO("Launching embedded HyperLinux %s before HyperCore (%zu bytes)",
           loader_name, loader_size);
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT, loader,
                                   loader_size)) {
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

static bool firmware_uses_hypervisor_940(uint32_t firmware_version) {
  return onion_firmware_route(firmware_version) ==
         ONION_FIRMWARE_ROUTE_HYPERVISOR_940;
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
      bootstrap_notify("notify.boot.component_failed", "HyperHEN Hypervisor");
      return false;
    }

    pid_t armed_pid = -1;
    if (onion_ready_read_pid(ONION_READY_HYPERVISOR_ARMED, &armed_pid) &&
        onion_proc_is_alive(armed_pid)) {
      missing_process_checks = 0;
    } else if (++missing_process_checks >= 10) {
      bootstrap_notify("notify.boot.component_incomplete", "HyperHEN Hypervisor");
      return false;
    }
    sleep(1);
  }
}

static void prepare_hypervisor_5xx_markers(void) {
  /* The supplied standalone payload is one-shot.  Keep its result sticky for
     this boot so a bootstrapper retry cannot run it against an already
     modified hypervisor.  /system_tmp clears these markers on full reboot. */
}

static void prepare_hypervisor_940_markers(void) {
  /*
   * The supplied payload stays resident to retain its mapped cave.  Keep its
   * session markers across a bootstrapper retry so a second invocation cannot
   * inject another copy while the first one is armed or already complete.
   * /system_tmp is reset by a full console restart.
   */
  if (onion_ready_is_set(ONION_READY_HYPERVISOR_940) ||
      onion_ready_is_set(ONION_READY_HYPERVISOR_940_ARMED) ||
      onion_ready_is_set(ONION_READY_HYPERVISOR_940_FAILED))
    return;
}

/* Returns 1 to the sole owner, 0 when another bootstrapper already owns the
   resident payload session, and -1 on a filesystem error. */
static int claim_hypervisor_940_session(void) {
  char path[256];
  (void)mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  (void)mkdir(ONION_READY_ROOT, 0777);
  if (!onion_ready_path(ONION_READY_HYPERVISOR_940_ARMED, path,
                        sizeof(path)))
    return -1;

  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    return errno == EEXIST ? 0 : -1;
  const char armed = '1';
  const bool written = write(fd, &armed, 1) == 1;
  close(fd);
  return written ? 1 : -1;
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

typedef struct Hypervisor940ReportSnapshot {
  uint64_t digest;
  size_t size;
  uint64_t device;
  uint64_t inode;
  int64_t mtime_sec;
  int64_t mtime_nsec;
  int64_t ctime_sec;
  int64_t ctime_nsec;
  bool ready;
} Hypervisor940ReportSnapshot;

static bool hypervisor_940_snapshots_equal(
    const Hypervisor940ReportSnapshot *left,
    const Hypervisor940ReportSnapshot *right) {
  return left->digest == right->digest && left->size == right->size &&
         left->device == right->device && left->inode == right->inode &&
         left->mtime_sec == right->mtime_sec &&
         left->mtime_nsec == right->mtime_nsec &&
         left->ctime_sec == right->ctime_sec &&
         left->ctime_nsec == right->ctime_nsec;
}

static bool hypervisor_940_same_report_generation(const struct stat *left,
                                                   const struct stat *right) {
  return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
         left->st_size == right->st_size &&
         left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
         left->st_mtim.tv_nsec == right->st_mtim.tv_nsec &&
         left->st_ctim.tv_sec == right->st_ctim.tv_sec &&
         left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
}

static bool hypervisor_940_read_report(
    Hypervisor940ReportSnapshot *snapshot) {
  int fd = open(HYPERHEN_HYPERVISOR_940_RESULT, O_RDONLY);
  if (fd < 0)
    return false;

  struct stat before;
  if (fstat(fd, &before) != 0 || before.st_size <= 0 ||
      (uint64_t)before.st_size > HYPERHEN_HYPERVISOR_940_RESULT_MAX) {
    close(fd);
    return false;
  }

  const size_t expected = (size_t)before.st_size;
  uint8_t *report = (uint8_t *)malloc(expected);
  if (!report) {
    close(fd);
    return false;
  }

  size_t used = 0;
  while (used < expected) {
    const ssize_t count = read(fd, report + used, expected - used);
    if (count > 0) {
      used += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR)
      continue;
    break;
  }
  struct stat after;
  const bool stable_generation =
      fstat(fd, &after) == 0 &&
      hypervisor_940_same_report_generation(&before, &after);
  close(fd);
  if (used != expected || !stable_generation) {
    free(report);
    return false;
  }

  uint64_t digest = UINT64_C(1469598103934665603);
  for (size_t i = 0; i < used; ++i) {
    digest ^= report[i];
    digest *= UINT64_C(1099511628211);
  }
  snapshot->digest = digest;
  snapshot->size = used;
  snapshot->device = (uint64_t)after.st_dev;
  snapshot->inode = (uint64_t)after.st_ino;
  snapshot->mtime_sec = (int64_t)after.st_mtim.tv_sec;
  snapshot->mtime_nsec = (int64_t)after.st_mtim.tv_nsec;
  snapshot->ctime_sec = (int64_t)after.st_ctim.tv_sec;
  snapshot->ctime_nsec = (int64_t)after.st_ctim.tv_nsec;
  snapshot->ready = onion_hypervisor_940_report_ready(report, used);
  free(report);
  return true;
}

static bool hypervisor_940_wait_initial_report(
    Hypervisor940ReportSnapshot *baseline) {
  Hypervisor940ReportSnapshot previous = {0};
  bool have_previous = false;
  int stable_polls = 0;

  for (int waited = 0; waited < 60000; waited += 200) {
    Hypervisor940ReportSnapshot current = {0};
    if (!hypervisor_940_read_report(&current)) {
      usleep(200000);
      continue;
    }
    if (current.ready) {
      *baseline = current;
      return true;
    }
    if (have_previous &&
        hypervisor_940_snapshots_equal(&current, &previous)) {
      if (++stable_polls >= 5) {
        *baseline = current;
        return true;
      }
    } else {
      previous = current;
      have_previous = true;
      stable_polls = 0;
    }
    usleep(200000);
  }
  return false;
}

static bool hypervisor_940_wait_post_resume(
    const Hypervisor940ReportSnapshot *baseline) {
  Hypervisor940ReportSnapshot candidate = {0};
  bool have_candidate = false;
  int stable_polls = 0;

  for (int waited = 0; waited < 900000; waited += 200) {
    Hypervisor940ReportSnapshot current = {0};
    if (!hypervisor_940_read_report(&current)) {
      usleep(200000);
      continue;
    }
    if (current.ready)
      return true;
    if (hypervisor_940_snapshots_equal(&current, baseline)) {
      have_candidate = false;
      stable_polls = 0;
      usleep(200000);
      continue;
    }
    if (have_candidate &&
        hypervisor_940_snapshots_equal(&current, &candidate)) {
      if (++stable_polls >= 5) {
        LOG_ERROR("Byepervisor 9.40 produced a stable non-success result");
        return false;
      }
    } else {
      candidate = current;
      have_candidate = true;
      stable_polls = 0;
    }
    usleep(200000);
  }
  LOG_ERROR("Byepervisor 9.40 resume result timed out");
  return false;
}

static int hypercore_ready_timeout_ms(uint32_t firmware_version) {
  /*
   * The exact A53/PPR path performs a mailbox discovery handshake followed by
   * verified DECI5S transactions.  A valid slow retry can exceed the old
   * fixed one-minute window, especially on 10.xx.  Other routes do not run
   * that installer and keep a shorter ceiling. The selected 5.xx HyperCore
   * path also runs the exact A53/PPR installer; the Byepervisor path never
   * calls this wait.
   */
  const OnionFirmwareRoute route = onion_firmware_route(firmware_version);
  return route == ONION_FIRMWARE_ROUTE_A53_PPR ||
                 route == ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX ||
                 route == ONION_FIRMWARE_ROUTE_LEGACY_HEN
             ? 600000
             : 180000;
}

static bool dedicated_hypercore_940_process_ready(void) {
  pid_t pid = -1;
  if (!onion_ready_read_pid(ONION_READY_HYPERCORE_940, &pid) ||
      !onion_proc_is_alive(pid))
    return false;

  char name[64] = {0};
  if (sceKernelGetProcessName((int)pid, name) != 0 ||
      strcmp(name, "kstuff.elf") != 0)
    return false;
  return kstuff_already_running();
}

static bool wait_for_hypercore_ready(uint32_t firmware_version) {
  const int poll_ms = 200;
  const int timeout_ms = hypercore_ready_timeout_ms(firmware_version);
  const OnionFirmwareRoute route = onion_firmware_route(firmware_version);
  const bool strict_selected_core =
      route == ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX ||
      route == ONION_FIRMWARE_ROUTE_LEGACY_HEN;
  bool starting_seen = false;
  bool marker_loss_logged = false;

  if (route == ONION_FIRMWARE_ROUTE_HYPERVISOR_940) {
    for (int waited = 0; waited < timeout_ms; waited += poll_ms) {
      if (onion_ready_is_set(ONION_READY_KSTUFF_FAILED))
        return false;
      if (dedicated_hypercore_940_process_ready()) {
        onion_ready_signal(ONION_READY_KSTUFF);
        LOG_DEBUG("Dedicated HyperCore 9.40 readiness verified after %d ms",
                  waited);
        return true;
      }

      pid_t starting_pid = -1;
      if (onion_ready_read_pid(ONION_READY_KSTUFF_STARTING, &starting_pid)) {
        starting_seen = true;
        if (!onion_proc_is_alive(starting_pid)) {
          LOG_ERROR("HyperCore 9.40 process exited before dedicated readiness");
          return false;
        }
      } else if (starting_seen) {
        LOG_ERROR("HyperCore 9.40 lost its startup identity before readiness");
        return false;
      }
      usleep((useconds_t)poll_ms * 1000U);
    }
    LOG_ERROR("Dedicated HyperCore 9.40 readiness timed out");
    return false;
  }

  for (int waited = 0; waited < timeout_ms; waited += poll_ms) {
    if (onion_ready_is_set(ONION_READY_KSTUFF_FAILED)) {
      if (strict_selected_core) {
        LOG_ERROR("Selected HyperCore published failure before full readiness");
        return false;
      }
      if (kstuff_already_running()) {
        onion_ready_clear(ONION_READY_KSTUFF_FAILED);
        onion_ready_signal(ONION_READY_KSTUFF);
        LOG_WARN("HyperCore failure marker superseded by a successful "
                 "capability probe");
        return true;
      }
      return false;
    }

    if (onion_ready_is_set(ONION_READY_KSTUFF)) {
      if (kstuff_already_running()) {
        LOG_DEBUG("HyperCore full readiness verified after %d ms", waited);
        return true;
      }
      if (strict_selected_core && (waited % 1000) == 0)
        LOG_WARN("Selected HyperCore readiness is published but the capability "
                 "probe is still pending");
    }

    /*
     * Preserve the release build's capability probe as the authoritative
     * compatibility fallback.  Older/slow loaders can activate the kernel
     * patches before publishing the newer lifecycle markers; blocking on the
     * marker alone prevents the common Automount/Backport stage from ever
     * being reached even though HyperCore is already usable.
     */
    if (!strict_selected_core && (waited % 1000) == 0 && kstuff_already_running()) {
      onion_ready_clear(ONION_READY_KSTUFF_FAILED);
      if (!onion_ready_signal(ONION_READY_KSTUFF))
        LOG_WARN("HyperCore capability is active but readiness publication "
                 "failed");
      LOG_DEBUG("HyperCore capability probe passed after %d ms", waited);
      return true;
    }

    pid_t starting_pid = -1;
    if (onion_ready_read_pid(ONION_READY_KSTUFF_STARTING, &starting_pid)) {
      starting_seen = true;
      if (!onion_proc_is_alive(starting_pid)) {
        if (!strict_selected_core && kstuff_already_running()) {
          onion_ready_signal(ONION_READY_KSTUFF);
          return true;
        }
        LOG_ERROR("HyperCore startup process pid=%d exited before readiness",
                  (int)starting_pid);
        return false;
      }
    } else if (starting_seen && !marker_loss_logged) {
      if (strict_selected_core) {
        LOG_ERROR("Selected HyperCore lost its startup identity before full "
                  "readiness");
        return false;
      }
      /* Continue with the published mprotect probe instead of suppressing the
       * shared Automount/Backport stage because a transient marker vanished. */
      LOG_WARN("HyperCore startup marker disappeared; continuing capability "
               "probe");
      marker_loss_logged = true;
    }

    usleep((useconds_t)poll_ms * 1000U);
  }

  LOG_ERROR("HyperCore startup exceeded its firmware-route timeout");
  return onion_ready_is_set(ONION_READY_KSTUFF) &&
         kstuff_already_running();
}

static bool launch_hypervisor_5xx(LaunchContext *context) {
  prepare_hypervisor_5xx_markers();

  if (onion_ready_is_set(ONION_READY_HYPERVISOR_5XX)) {
    LOG_INFO("Reusing completed Byepervisor 5.xx state from this boot");
    return true;
  }
  if (onion_ready_is_set(ONION_READY_HYPERVISOR_5XX_FAILED)) {
    LOG_ERROR("Byepervisor 5.xx already failed in this boot");
    bootstrap_notify("notify.boot.component_restart", "HyperHEN Byepervisor 5.xx");
    return false;
  }
  if (access(HYPERHEN_HYPERVISOR_5XX_STAGE, F_OK) == 0) {
    LOG_ERROR("Byepervisor 5.xx has an unfinished launch in this boot");
    onion_ready_signal(ONION_READY_HYPERVISOR_5XX_FAILED);
    bootstrap_notify("notify.boot.component_restart", "HyperHEN Byepervisor 5.xx");
    return false;
  }

  /* The supplied 5.xx payload is a standalone ELF and does not publish the
     HyperHEN readiness markers.  Launch it through the private loader so the
     returned PID identifies this exact instance, then serialize Automount and
     the Dashboard services behind the payload's normal process exit. */
  if (context->loader_port != ONION_ELFLDR_PORT && ensure_private_loader())
    context->loader_port = ONION_ELFLDR_PORT;
  if (context->loader_port != ONION_ELFLDR_PORT ||
      !elfldr_remote_onion_available()) {
    LOG_ERROR("Byepervisor 5.xx needs the private loader for exact PID tracking");
    onion_ready_signal(ONION_READY_HYPERVISOR_5XX_FAILED);
    bootstrap_notify("notify.boot.component_unavailable", "HyperHEN Byepervisor 5.xx");
    return false;
  }

  LOG_DEBUG("Loading embedded HyperHEN Byepervisor 5.xx via %u ...",
            context->loader_port);
  const pid_t pid = elfldr_remote_onion_write_and_launch_get_pid(
      HYPERHEN_HYPERVISOR_5XX_STAGE, hypervisor_5xx_start,
      (size_t)hypervisor_5xx_size);
  if (pid <= 1) {
    onion_ready_signal(ONION_READY_HYPERVISOR_5XX_FAILED);
    bootstrap_notify("notify.boot.component_launch_failed", "HyperHEN Byepervisor 5.xx");
    return false;
  }

  const int poll_ms = 100;
  bool completed = false;
  for (int waited = 0; waited < HYPERHEN_HYPERVISOR_5XX_EXIT_TIMEOUT_MS;
       waited += poll_ms) {
    if (onion_ready_is_set(ONION_READY_HYPERVISOR_5XX_FAILED))
      return false;
    if (!onion_proc_is_alive(pid)) {
      completed = true;
      break;
    }
    usleep((useconds_t)poll_ms * 1000U);
  }

  if (!completed) {
    LOG_ERROR("Byepervisor 5.xx pid=%d did not complete before timeout",
              (int)pid);
    onion_ready_signal(ONION_READY_HYPERVISOR_5XX_FAILED);
    bootstrap_notify("notify.boot.component_incomplete", "HyperHEN Byepervisor 5.xx");
    return false;
  }

  if (unlink(HYPERHEN_HYPERVISOR_5XX_STAGE) != 0 && errno != ENOENT)
    LOG_WARN("Cannot remove completed Byepervisor 5.xx stage: %s",
             strerror(errno));

  /* Give kernel-side state a short settling interval after the standalone
     process exits before Automount and the Dashboard services consume it. */
  usleep(500000U);
  if (!onion_ready_signal(ONION_READY_HYPERVISOR_5XX)) {
    onion_ready_signal(ONION_READY_HYPERVISOR_5XX_FAILED);
    bootstrap_notify("notify.boot.component_publish_failed", "HyperHEN Byepervisor 5.xx");
    return false;
  }

  LOG_INFO("Byepervisor 5.xx completed; Automount may start");
  return true;
}

static bool launch_hypervisor_940(LaunchContext *context,
                                  uint32_t firmware_version) {
  if (!firmware_uses_hypervisor_940(firmware_version))
    return true;

  prepare_hypervisor_940_markers();
  Hypervisor940ReportSnapshot baseline = {0};
  if (onion_ready_is_set(ONION_READY_HYPERVISOR_940)) {
    if (hypervisor_940_read_report(&baseline) && baseline.ready) {
      LOG_INFO("Reusing verified Byepervisor 9.40 state from this session");
      return true;
    }
    LOG_ERROR("Byepervisor 9.40 ready marker has no verified result");
    onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    bootstrap_notify("notify.boot.component_restart", "HyperHEN Byepervisor 9.40");
    return false;
  }
  if (onion_ready_is_set(ONION_READY_HYPERVISOR_940_FAILED)) {
    LOG_ERROR("Byepervisor 9.40 already failed in this boot session");
    bootstrap_notify("notify.boot.component_restart", "HyperHEN Byepervisor 9.40");
    return false;
  }

  const bool already_armed =
      onion_ready_is_set(ONION_READY_HYPERVISOR_940_ARMED);
  if (already_armed) {
    /* There is no trustworthy process identity or persisted pre-resume
       baseline for a different bootstrapper instance.  Never associate a
       persistent /data report with somebody else's resident payload. */
    onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    bootstrap_notify("notify.boot.component_already_active", "HyperHEN Byepervisor 9.40");
    return false;
  }

  const int claim = claim_hypervisor_940_session();
  if (claim <= 0) {
    if (claim < 0)
      onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    bootstrap_notify(claim == 0
                         ? "notify.boot.component_starting"
                         : "notify.boot.component_session_failed",
                     "HyperHEN Byepervisor 9.40");
    return false;
  }
  if (unlink(HYPERHEN_HYPERVISOR_940_RESULT) != 0 && errno != ENOENT) {
    LOG_ERROR("Cannot clear stale Byepervisor 9.40 report: %s",
              strerror(errno));
    onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    return false;
  }
  LOG_DEBUG("Loading pinned HyperHEN Byepervisor 9.40 via %u ...",
            context->loader_port);
  if (!launch_blob(context, hypervisor_940_start,
                   (size_t)hypervisor_940_size,
                   "HyperHEN Byepervisor 9.40", NULL)) {
    /* The remote send may have crossed the execution boundary even when its
       acknowledgement is lost.  Keep ARMED sticky and require a reboot. */
    onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    bootstrap_notify("notify.boot.component_launch_failed", "HyperHEN Byepervisor 9.40");
    return false;
  }

  if (!hypervisor_940_wait_initial_report(&baseline)) {
    onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    bootstrap_notify("notify.boot.component_not_armed", "HyperHEN Byepervisor 9.40");
    return false;
  }

  bool ready = baseline.ready;
  if (!ready) {
    bootstrap_notify("notify.boot.component_rest_mode", "HyperHEN Byepervisor 9.40");
    ready = hypervisor_940_wait_post_resume(&baseline);
  }
  if (!ready) {
    onion_ready_signal(ONION_READY_HYPERVISOR_940_FAILED);
    bootstrap_notify("notify.boot.component_resume_failed", "HyperHEN Byepervisor 9.40");
    return false;
  }

  if (!onion_ready_signal(ONION_READY_HYPERVISOR_940)) {
    bootstrap_notify("notify.boot.component_publish_failed", "HyperHEN Byepervisor 9.40");
    return false;
  }
  onion_ready_clear(ONION_READY_HYPERVISOR_940_ARMED);
  LOG_INFO("Byepervisor 9.40 post-resume result verified");
  return true;
}

static bool launch_legacy_hen(LaunchContext *context) {
  prepare_legacy_hen_markers();
  LOG_DEBUG("Loading embedded HyperHEN legacy engine via %u ...",
            context->loader_port);
  if (!launch_blob(context, legacy_hen_start, (size_t)legacy_hen_size,
                   "HyperHEN legacy engine", NULL)) {
    bootstrap_notify("notify.boot.component_launch_failed", "HyperHEN Legacy");
    return false;
  }

  return wait_for_stage_ready(ONION_READY_LEGACY_HEN,
                              ONION_READY_LEGACY_HEN_FAILED, 60000);
}

static bool launch_automount(LaunchContext *context,
                             uint32_t firmware_version,
                             enum HyperhenAutomountEngine engine) {
  if (!external_automount_preflight()) {
    /* A service started during the core wait may now rely on the sentinel.
     * Keep it in place; do not perform either provider's teardown here. */
    context->automount_sentinel_created = false;
    return false;
  }
  const bool lite = onion_firmware_route(firmware_version) ==
                    ONION_FIRMWARE_ROUTE_KSTUFF_LITE;
  const uint8_t *elf = lite ? automount_lite_start : automount_start;
  const size_t size = lite ? (size_t)automount_lite_size
                           : (size_t)automount_size;
  prepare_automount_markers();
  LOG_DEBUG("Starting embedded HyperHEN Automount%s via %u ...",
            lite ? " lite" : "", context->loader_port);
  struct HyperhenAutomountRouteProof proof = {0};
  /* Both callers reach this point only after the selected engine's original
   * readiness checks. This handoff describes that result; it cannot bypass it. */
  if (!hyperhen_automount_route_publish(ONION_READY_ROOT, firmware_version,
                                        engine, lite, getpid(), &proof)) {
    LOG_ERROR("Cannot publish selected Automount launch route: %s", strerror(errno));
    bootstrap_notify("notify.boot.component_publish_failed", "HyperHEN Automount");
    return false;
  }
  if (!launch_blob(context, elf, size,
                   lite ? "HyperHEN Automount lite" : "HyperHEN Automount",
                   NULL)) {
    if (!hyperhen_automount_route_clear(&proof))
      LOG_WARN("Automount launch-route proof cleanup failed after dispatch: %s",
               strerror(errno));
    bootstrap_notify("notify.boot.component_launch_failed", "HyperHEN Automount");
    return false;
  }

  const bool ready = wait_for_stage_ready(ONION_READY_AUTOMOUNT,
                                         ONION_READY_AUTOMOUNT_FAILED, 180000);
  if (!hyperhen_automount_route_clear(&proof))
    LOG_WARN("Automount launch-route proof cleanup failed after readiness wait: %s",
             strerror(errno));
  if (!ready) {
    if (!onion_ready_is_set(ONION_READY_AUTOMOUNT_FAILED))
      onion_ready_signal(ONION_READY_AUTOMOUNT_FAILED);
    bootstrap_notify("notify.boot.component_failed", "HyperHEN Automount");
    return false;
  }
  if (onion_ready_is_set("automount_limited"))
    LOG_WARN("Automount beta9 limited mode on fw=0x%08x: scanner/API ready, "
             "on-demand ShellCore hooks unavailable", firmware_version & 0xffff0000U);
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
    const bool exact_1270 =
        (firmware_version & 0xffff0000U) == 0x12700000U;
    const uint8_t *elf = exact_1270 ? hypervisor_1270_start
                                    : hypervisor_start;
    const size_t size = exact_1270 ? (size_t)hypervisor_1270_size
                                   : (size_t)hypervisor_size;
    onion_ready_clear(ONION_READY_HYPERVISOR_ARMED);
    onion_ready_clear(ONION_READY_HYPERVISOR_FAILED);
    LOG_DEBUG("Loading embedded HyperHEN Hypervisor%s via %u ...",
              exact_1270 ? " 12.70" : "", context->loader_port);
    if (!launch_blob(context, elf, size,
                     exact_1270 ? "HyperHEN Hypervisor 12.70"
                                : "HyperHEN Hypervisor",
                     NULL)) {
      bootstrap_notify("notify.boot.component_launch_failed", "HyperHEN Hypervisor");
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
    bootstrap_notify("notify.boot.component_not_armed", "HyperHEN Hypervisor");
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
  onion_ready_clear(ONION_READY_HYPERCORE_LITE);
  prepare_hypervisor_markers();
  prepare_hypervisor_5xx_markers();
  prepare_hypervisor_940_markers();
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
    bootstrap_notify("notify.boot.component_incomplete", "HyperHEN Util");
    return false;
  }
  return true;
}

static bool launch_hypercore(LaunchContext *context,
                             uint32_t firmware_version, bool autoload) {
  const OnionFirmwareRoute route = onion_firmware_route(firmware_version);
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
    if (route == ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX ||
        route == ONION_FIRMWARE_ROUTE_LEGACY_HEN) {
      if (onion_ready_is_set(ONION_READY_KSTUFF_FAILED) ||
          !onion_ready_is_set(ONION_READY_KSTUFF)) {
        LOG_ERROR("Refusing partial selected HyperCore state; full restart is "
                  "required");
        return false;
      }
      LOG_INFO("Reusing fully completed selected HyperCore state");
      return true;
    }
    if (route == ONION_FIRMWARE_ROUTE_HYPERVISOR_940) {
      if (!dedicated_hypercore_940_process_ready()) {
        LOG_ERROR("A generic core capability is active without the dedicated "
                  "HyperCore 9.40 identity");
        return false;
      }
      onion_ready_clear(ONION_READY_KSTUFF_FAILED);
      onion_ready_signal(ONION_READY_KSTUFF);
      return true;
    }
    LOG_WARN("HyperCore capability already active; restoring runtime "
             "readiness and continuing to Automount/Backport");
    onion_ready_clear(ONION_READY_KSTUFF_FAILED);
    onion_ready_signal(ONION_READY_KSTUFF);
    return true;
  }

  onion_ready_clear(ONION_READY_KSTUFF);
  onion_ready_clear(ONION_READY_KSTUFF_FAILED);
  onion_ready_clear(ONION_READY_KSTUFF_STARTING);
  if (route == ONION_FIRMWARE_ROUTE_HYPERVISOR_940)
    onion_ready_clear(ONION_READY_HYPERCORE_940);

  LOG_DEBUG("Loading embedded HyperCore via %u (before daemon/toolbox) ...",
            context->loader_port);
  const bool lite = route == ONION_FIRMWARE_ROUTE_KSTUFF_LITE;
  const bool dedicated_core =
      onion_firmware_uses_dedicated_hypercore(firmware_version);
  const bool dedicated_940 =
      dedicated_core && route == ONION_FIRMWARE_ROUTE_HYPERVISOR_940;
  const bool dedicated_1270 =
      dedicated_core &&
      (firmware_version & 0xffff0000U) == 0x12700000U;
  const uint8_t *elf = dedicated_940
                           ? kstuff_940_start
                           : (dedicated_1270
                                  ? kstuff_1270_start
                                  : (lite ? kstuff_lite_start : kstuff_start));
  const size_t size = dedicated_940
                          ? (size_t)kstuff_940_size
                          : (dedicated_1270
                                 ? (size_t)kstuff_1270_size
                                 : (lite ? (size_t)kstuff_lite_size
                                         : (size_t)kstuff_size));
  const bool sent = launch_blob(context, elf, size,
                                lite ? "HyperCore lite"
                                     : (dedicated_940 ? "HyperCore 9.40"
                                        : (dedicated_1270
                                               ? "HyperCore 12.70"
                                               : "HyperCore")),
                                NULL);
  if (!sent) {
    bootstrap_notify("notify.kstuff.load_elfldr_failed");
    return false;
  }

  if (!wait_for_hypercore_ready(firmware_version)) {
    bootstrap_notify("notify.boot.component_incomplete", "HyperCore ShellCore");
    return false;
  }

  if (!kstuff_already_running()) {
    LOG_ERROR("HyperCore published readiness without the mprotect capability");
    onion_ready_clear(ONION_READY_KSTUFF);
    onion_ready_signal(ONION_READY_KSTUFF_FAILED);
    bootstrap_notify("notify.boot.component_readiness_failed", "HyperCore");
    return false;
  }

  LOG_DEBUG("HyperCore readiness marker and mprotect probe verified");
  return true;
}

static bool prepare_hypercore_mode(uint32_t firmware_version) {
  const OnionFirmwareRoute route = onion_firmware_route(firmware_version);
  onion_ready_clear(ONION_READY_HYPERCORE_NO_PPR);
  onion_ready_clear(ONION_READY_HYPERCORE_LITE);
  if (route == ONION_FIRMWARE_ROUTE_KSTUFF_LITE) {
    if (!onion_ready_signal(ONION_READY_HYPERCORE_LITE)) {
      LOG_ERROR("Cannot publish HyperCore lite route marker");
      bootstrap_notify("notify.boot.component_prepare_failed", "HyperHEN kstuff-lite");
      return false;
    }
    LOG_WARN("FW 0x%08x uses the dedicated kstuff-lite route; PPR and "
             "BackPork are disabled",
             firmware_version & 0xffff0000U);
    return true;
  }
  if (route != ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR) {
    return true;
  }

  if (!onion_ready_signal(ONION_READY_HYPERCORE_NO_PPR)) {
    LOG_ERROR("Cannot publish HyperCore no-PPR compatibility marker");
    bootstrap_notify("notify.boot.component_prepare_failed", "HyperHEN HyperCore");
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
    bootstrap_notify("notify.boot.component_incomplete", "HyperHEN Daemon");
    return false;
  }
  return true;
}

static bool launch_hyperdpi(LaunchContext *context) {
  LOG_DEBUG("Starting embedded HyperDPI via %u ...", context->loader_port);
  if (!launch_blob(context, hyperdpi_start, (size_t)hyperdpi_size,
                   "HyperDPI", NULL)) {
    bootstrap_notify("notify.boot.component_launch_failed", "HyperDPI");
    return false;
  }
  return true;
}

static bool validate_5xx_engine_state(Onion5xxBootMode mode) {
  if (mode == ONION_5XX_BOOT_MODE_BYPERVISOR) {
    if (onion_ready_is_set(ONION_READY_KSTUFF) ||
        onion_ready_is_set(ONION_READY_KSTUFF_STARTING) ||
        onion_ready_is_set(ONION_READY_KSTUFF_FAILED) ||
        kstuff_already_running()) {
      LOG_ERROR("Refusing Byepervisor after HyperCore activity in this boot");
      bootstrap_notify("notify.boot.restart_before_choice", "Byepervisor");
      return false;
    }
    return true;
  }

  if (mode == ONION_5XX_BOOT_MODE_HYPERCORE) {
    if (onion_ready_is_set(ONION_READY_HYPERVISOR_5XX) ||
        onion_ready_is_set(ONION_READY_HYPERVISOR_5XX_FAILED) ||
        access(HYPERHEN_HYPERVISOR_5XX_STAGE, F_OK) == 0) {
      LOG_ERROR("Refusing HyperCore after Byepervisor activity in this boot");
      bootstrap_notify("notify.boot.restart_before_choice", "HyperCore");
      return false;
    }
    return true;
  }

  return false;
}

int bootstrap_launch_services(uint32_t firmware_version, bool kstuff_autoload,
                              Onion5xxBootMode boot_mode_5xx) {
  LOG_INFO("HyperHEN %s R29 ShellUI startup guard (Automount beta9 retained) fw=0x%08x",
           HYPERHEN_VERSION, firmware_version & 0xffff0000U);
  const OnionFirmwareRoute route = onion_firmware_route(firmware_version);
  const uint32_t capabilities = onion_firmware_capabilities(
      onion_engine_selected_route(firmware_version, boot_mode_5xx));
  const bool is_legacy = firmware_uses_legacy_hen(firmware_version);
  const bool use_legacy_hypercore =
      is_legacy && boot_mode_5xx == ONION_5XX_BOOT_MODE_HYPERCORE;
  const bool is_5xx = firmware_uses_hypervisor_5xx(firmware_version);
  const bool use_5xx_bypervisor =
      is_5xx && boot_mode_5xx == ONION_5XX_BOOT_MODE_BYPERVISOR;
  const bool use_5xx_hypercore =
      is_5xx && boot_mode_5xx == ONION_5XX_BOOT_MODE_HYPERCORE;
  const bool defer_util_until_kernel_ready = use_5xx_bypervisor;
  const uint32_t required_mount_capabilities =
      route == ONION_FIRMWARE_ROUTE_KSTUFF_LITE
          ? ONION_FIRMWARE_CAP_AUTOMOUNT
          : (use_5xx_bypervisor
                 ? ONION_FIRMWARE_CAP_AUTOMOUNT
                 : ONION_FIRMWARE_CAP_AUTOMOUNT |
                       ONION_FIRMWARE_CAP_BACKPORT);

  if (route == ONION_FIRMWARE_ROUTE_UNSUPPORTED) {
    LOG_ERROR("No exact HyperHEN route for firmware 0x%08x",
              firmware_version & 0xffff0000U);
    bootstrap_notify("notify.boot.no_profile");
    return -3;
  }

  if ((onion_engine_selector_supported(firmware_version) &&
       !onion_engine_selector_accepts(firmware_version, boot_mode_5xx)) ||
      (!onion_engine_selector_supported(firmware_version) &&
       boot_mode_5xx != ONION_5XX_BOOT_MODE_DEFAULT)) {
    LOG_ERROR("Invalid engine selection %d for firmware 0x%08x",
              (int)boot_mode_5xx, firmware_version & 0xffff0000U);
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware_version, "invalid_restart"));
    return -3;
  }

  if (is_5xx && !validate_5xx_engine_state(boot_mode_5xx))
    return -3;
  if (is_legacy) {
    const bool other_engine_active = use_legacy_hypercore
        ? (onion_ready_is_set(ONION_READY_LEGACY_HEN) ||
           onion_ready_is_set(ONION_READY_LEGACY_HEN_FAILED))
        : (onion_ready_is_set(ONION_READY_KSTUFF) ||
           onion_ready_is_set(ONION_READY_KSTUFF_STARTING) ||
           onion_ready_is_set(ONION_READY_KSTUFF_FAILED));
    if (other_engine_active) {
      LOG_ERROR("Refusing a different legacy engine in the same boot session");
      bootstrap_notify("notify.boot.restart_before_choice",
                       use_legacy_hypercore ? "HyperCore" : "PS5 HEN");
      return -3;
    }
  }

  /* Verify a live named process before private loaders, marker preparation,
   * mount ownership or firmware engines can change the current session. */
  if (!external_automount_preflight())
    return -3;

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
    bootstrap_notify("notify.boot.userland_only");
    return BOOTSTRAP_LAUNCH_USERLAND_ONLY;
  }

  if ((capabilities & required_mount_capabilities) !=
      required_mount_capabilities) {
    LOG_ERROR("Firmware route lacks the required Automount/Backport stack");
    bootstrap_notify("notify.boot.missing_compatibility");
    return -3;
  }

  if (!claim_automount_ownership(&context)) {
    bootstrap_notify("notify.boot.automount_ownership");
    return -2;
  }
  if (is_legacy && !use_legacy_hypercore) {
    if (!launch_legacy_hen(&context)) {
      release_automount_ownership(&context);
      bootstrap_notify("notify.boot.component_failed", "PS5 HEN");
      return -2;
    }
    if ((capabilities & ONION_FIRMWARE_CAP_UTIL) != 0 &&
        !launch_util(&context)) {
      release_automount_ownership(&context);
      return -2;
    }
    if (!launch_automount(&context, firmware_version,
                          HYPERHEN_AUTOMOUNT_ENGINE_LEGACY)) {
      release_automount_ownership(&context);
      return -2;
    }
    if ((capabilities & ONION_FIRMWARE_CAP_DAEMON) != 0 &&
        !launch_daemon(&context))
      return -2;
    bootstrap_notify("notify.boot.component_active", "PS5 HEN + Dashboard");
    return 0;
  }
  if (!launch_hypervisor_940(&context, firmware_version)) {
    release_automount_ownership(&context);
    bootstrap_notify("notify.boot.component_failed", "HyperHEN Byepervisor 9.40");
    return -2;
  }
  if (!defer_util_until_kernel_ready &&
      (capabilities & ONION_FIRMWARE_CAP_UTIL) != 0 &&
      !launch_util(&context)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (use_5xx_bypervisor && !launch_hypervisor_5xx(&context)) {
    release_automount_ownership(&context);
    bootstrap_notify("notify.boot.component_failed", "HyperHEN Byepervisor 5.xx");
    return -2;
  }
  if (!launch_hypervisor(&context, firmware_version)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (!use_5xx_bypervisor) {
    if (!prepare_hypercore_mode(firmware_version)) {
      release_automount_ownership(&context);
      return -2;
    }
    const bool hypercore_autoload =
        use_5xx_hypercore || use_legacy_hypercore || kstuff_autoload;
    if (!launch_hypercore(&context, firmware_version, hypercore_autoload)) {
      onion_ready_clear(ONION_READY_HYPERCORE_NO_PPR);
      onion_ready_clear(ONION_READY_HYPERCORE_LITE);
      release_automount_ownership(&context);
      bootstrap_notify("notify.boot.automount_requires_core");
      return -2;
    }
  } else {
    LOG_INFO("5.xx Byepervisor mode: HyperCore, A53/PPR and BackPork skipped");
  }
  if (route == ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR) {
    bootstrap_notify("notify.boot.compatibility_active");
  }
  if (!launch_automount(&context, firmware_version,
                        use_5xx_bypervisor ? HYPERHEN_AUTOMOUNT_ENGINE_BYPERVISOR
                                            : HYPERHEN_AUTOMOUNT_ENGINE_CORE)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (defer_util_until_kernel_ready &&
      (capabilities & ONION_FIRMWARE_CAP_UTIL) != 0 &&
      !launch_util(&context)) {
    release_automount_ownership(&context);
    return -2;
  }
  if (route == ONION_FIRMWARE_ROUTE_KSTUFF_LITE) {
    LOG_DEBUG("HyperHEN kstuff-lite + Automount active; starting Dashboard "
              "services");
  }
  if ((capabilities & ONION_FIRMWARE_CAP_DAEMON) != 0 &&
      !launch_daemon(&context))
    return -2;
  if ((capabilities & ONION_FIRMWARE_CAP_HYPERDPI) != 0 &&
      !launch_hyperdpi(&context))
    LOG_WARN("HyperDPI is unavailable; core HyperHEN services remain active");
  if (use_5xx_bypervisor)
    bootstrap_notify("notify.boot.component_active", "HyperHEN Byepervisor + Dashboard");
  else if (use_5xx_hypercore || use_legacy_hypercore)
    bootstrap_notify("notify.boot.component_active", "HyperHEN HyperCore A53/PPR + Dashboard");
  if (route == ONION_FIRMWARE_ROUTE_KSTUFF_LITE) {
    bootstrap_notify("notify.boot.component_active", "HyperHEN kstuff-lite + Dashboard");
    return BOOTSTRAP_LAUNCH_CORE_ONLY;
  }
  return 0;
}
