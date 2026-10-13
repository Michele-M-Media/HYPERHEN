/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Independent fan-threshold maintainer.
 *
 * /dev/icc_fan ioctl 0xC01C8F07 is a one-shot write. Firmware reasserts its
 * own curve shortly afterwards, so a dedicated thread rewrites the configured
 * threshold while cooling.fan_control=temperature_threshold. This must not
 * live inside the app-jailbreak listener.
 */

#include "daemon_ops.hpp"
#include "globalconf.hpp"

#include <onion/log.h>
#include <onion/notify.h>
#include <onion/settings.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr int kFanIoctl = 0xC01C8F07;
constexpr unsigned kFanRewriteSeconds = 2;

enum class FanWriteResult {
  Ok,
  OpenFailed,
  IoctlFailed,
};

FanWriteResult write_icc_fan_threshold(int temp) {
  temp = onion::clamp_fan_threshold(temp);

  const int fd = open("/dev/icc_fan", O_RDONLY, 0);
  if (fd < 0)
    return FanWriteResult::OpenFailed;

  char data[10] = {};
  data[5] = static_cast<char>(temp);
  const int rc = ioctl(fd, kFanIoctl, data);
  const int ioctl_err = errno;
  close(fd);
  if (rc < 0) {
    errno = ioctl_err;
    return FanWriteResult::IoctlFailed;
  }
  return FanWriteResult::Ok;
}

} // namespace

bool set_fan_threshold(int temp) {
  switch (write_icc_fan_threshold(temp)) {
  case FanWriteResult::Ok:
    return true;
  case FanWriteResult::OpenFailed:
    LOG_ERROR("fan: open /dev/icc_fan failed: %s", strerror(errno));
    onion_notify(true, "notify.fan.open_failed");
    return false;
  case FanWriteResult::IoctlFailed:
    LOG_ERROR("fan: ioctl failed: %s", strerror(errno));
    onion_notify(true, "notify.fan.set_failed");
    return false;
  }
  return false;
}

bool restore_automatic_fan() {
  return set_fan_threshold(onion::kFanAutomaticThresholdCelsius);
}

void *fan_maintenance_thread(void *args) noexcept {
  (void)args;

  bool holding = false;
  bool logged_write_error = false;
  int held_threshold = -1;

  LOG_INFO("fan maintenance started (rewrite every %u s)", kFanRewriteSeconds);

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const onion::Settings cfg = g_settings.snapshot();
    if (cfg.enable_fan_speed) {
      if (!holding || held_threshold != cfg.fan_threshold) {
        LOG_DEBUG("fan maintenance: holding threshold %d C", cfg.fan_threshold);
        holding = true;
        held_threshold = cfg.fan_threshold;
        logged_write_error = false;
      }
      if (write_icc_fan_threshold(cfg.fan_threshold) == FanWriteResult::Ok)
        logged_write_error = false;
      else if (!logged_write_error) {
        LOG_ERROR("fan maintenance: rewrite failed: %s", strerror(errno));
        logged_write_error = true;
      }
    } else if (holding) {
      LOG_DEBUG("fan maintenance: idle (automatic)");
      holding = false;
      held_threshold = -1;
      logged_write_error = false;
    }

    /* Same style as runtime_supervisor: sleep the interval, check the
       shutdown flag on the next loop. Stack teardown exits the process. */
    sleep(kFanRewriteSeconds);
  }

  return nullptr;
}
