#include "sm_platform.h"

#include "sm_hyperhen_launch.h"
#include "sm_kstuff.h"
#include "sm_log.h"
#include "sm_runtime.h"

#define READY_DIRECTORY "/system_tmp/hyperhen/ready/"

static bool engine_marker_ready(const char *ready, const char *failed) {
  return access(ready, F_OK) == 0 && access(failed, F_OK) != 0 && errno == ENOENT;
}

bool sm_hyperhen_launch_route_ready(void) {
  char proof[160];
  int fd = open(SM_HYPERHEN_ROUTE_FILE, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) {
    log_debug("[HYPERHEN] missing launch-route proof: %s", strerror(errno));
    return false;
  }
  struct stat information;
  bool valid = fstat(fd, &information) == 0 && S_ISREG(information.st_mode) &&
               information.st_size > 0 && information.st_size < (off_t)sizeof(proof);
  size_t size = 0;
  while (valid && size < (size_t)information.st_size) {
    ssize_t amount = read(fd, proof + size, (size_t)information.st_size - size);
    if (amount < 0 && errno == EINTR)
      continue;
    if (amount <= 0) {
      valid = false;
      break;
    }
    size += (size_t)amount;
  }
  if (close(fd) != 0)
    valid = false;
  if (!valid) {
    log_debug("[HYPERHEN] invalid launch-route proof file");
    return false;
  }
  proof[size] = '\0';
  unsigned int firmware = 0, lite = 0;
  long launcher = 0;
  char route[16] = {0};
  int consumed = 0;
  if (sscanf(proof, "v1 %8x %15s %u %ld %n", &firmware, route, &lite,
             &launcher, &consumed) != 4 || consumed <= 0 ||
      (size_t)consumed != size || lite > 1 || launcher <= 1 || launcher > INT_MAX ||
      (uint32_t)firmware != (kernel_get_fw_version() & 0xffff0000u) ||
      (lite != 0) != sm_hyperhen_lite_profile() || kill((pid_t)launcher, 0) != 0) {
    log_debug("[HYPERHEN] launch-route proof rejected");
    return false;
  }

  const uint32_t major = (firmware >> 24) & 0xffu;
  bool ready = false;
  if (strcmp(route, "core") == 0) {
    if (engine_marker_ready(READY_DIRECTORY "kstuff", READY_DIRECTORY "kstuff_failed")) {
      ready = sm_kstuff_is_loaded();
      if (!ready)
        notify_system_l10n(SM_L10N_KSTUFF_WAITING);
      for (unsigned attempt = 0; !ready && attempt < 50; ++attempt) {
        if (sleep_with_stop_check(200000u))
          break;
        ready = sm_kstuff_is_loaded();
      }
      ready = ready &&
              engine_marker_ready(READY_DIRECTORY "kstuff", READY_DIRECTORY "kstuff_failed");
    }
  } else if (strcmp(route, "legacy") == 0) {
    /* The old 1.xx--4.xx engine provides the privileged APIs used by the
     * pre-8.40 ShellCore bridge; no remote Kstuff mlock is used on this route. */
    ready = major >= 0x01u && major <= 0x04u &&
            engine_marker_ready(READY_DIRECTORY "legacy_hen",
                                READY_DIRECTORY "legacy_hen_failed");
  } else if (strcmp(route, "bypervisor") == 0) {
    ready = major == 0x05u &&
            engine_marker_ready(READY_DIRECTORY "hypervisor_5xx",
                                READY_DIRECTORY "hypervisor_5xx_failed");
  }
  log_debug("[HYPERHEN] launch route=%s fw=0x%08x lite=%u proof=%s",
            route, firmware, lite, ready ? "accepted" : "rejected");
  return ready;
}
