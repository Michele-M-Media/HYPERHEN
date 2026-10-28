/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.
*/

#include "bootstrap_assets.h"
#include "bootstrap_config.h"
#include "bootstrap_filesystem.h"
#include "bootstrap_hyperdeck.h"
#include "bootstrap_notify.h"
#include "bootstrap_runtime.h"
#include "launch_pipeline.h"
#include "payload_autostart.h"

#include <onion/conflict.h>
#include <onion/firmware_capabilities.h>
#include <onion/engine_selector.h>
#include <onion/log.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool read_5xx_owner(pid_t *owner) {
  if (!owner)
    return false;
  const int fd =
      open(ONION_5XX_BOOT_OWNER_PATH, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return false;

  struct stat info {};
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
      info.st_size >= 32) {
    (void)close(fd);
    return false;
  }

  char value[32] = {0};
  size_t used = 0;
  while (used < static_cast<size_t>(info.st_size)) {
    const ssize_t chunk =
        read(fd, value + used, static_cast<size_t>(info.st_size) - used);
    if (chunk > 0) {
      used += static_cast<size_t>(chunk);
      continue;
    }
    if (chunk < 0 && errno == EINTR)
      continue;
    (void)close(fd);
    return false;
  }
  char extra = 0;
  ssize_t trailing = -1;
  do {
    trailing = read(fd, &extra, 1);
  } while (trailing < 0 && errno == EINTR);
  (void)close(fd);
  if (trailing != 0)
    return false;

  int64_t parsed = 0;
  for (size_t index = 0; index < used; ++index) {
    if (value[index] < '0' || value[index] > '9')
      return false;
    parsed = parsed * 10 + (value[index] - '0');
    if (parsed > INT32_MAX)
      return false;
  }
  if (parsed <= 1)
    return false;
  *owner = static_cast<pid_t>(parsed);
  return true;
}

bool claim_5xx_boot(void) {
  (void)mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  char temporary[256];
  const int temporary_length =
      std::snprintf(temporary, sizeof(temporary), "%s.tmp.%d",
                    ONION_5XX_BOOT_OWNER_PATH, static_cast<int>(getpid()));
  if (temporary_length <= 0 ||
      static_cast<size_t>(temporary_length) >= sizeof(temporary))
    return false;

  const int fd =
      open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (fd < 0)
    return false;

  char value[32];
  const int length = std::snprintf(value, sizeof(value), "%d",
                                   static_cast<int>(getpid()));
  size_t written = 0;
  while (length > 0 && static_cast<size_t>(length) < sizeof(value) &&
         written < static_cast<size_t>(length)) {
    const ssize_t chunk =
        write(fd, value + written, static_cast<size_t>(length) - written);
    if (chunk > 0) {
      written += static_cast<size_t>(chunk);
      continue;
    }
    if (chunk < 0 && errno == EINTR)
      continue;
    break;
  }
  const bool content_saved =
      length > 0 && static_cast<size_t>(length) < sizeof(value) &&
      written == static_cast<size_t>(length) && fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  if (!content_saved || !closed) {
    (void)unlink(temporary);
    return false;
  }

  if (link(temporary, ONION_5XX_BOOT_OWNER_PATH) == 0) {
    (void)unlink(temporary);
    return true;
  }
  const int publish_error = errno;
  (void)unlink(temporary);
  if (publish_error != EEXIST)
    return false;

  pid_t owner = -1;
  /* /system_tmp is reset by a full reboot.  Never reap a dead owner here: two
   * simultaneous reapers could otherwise remove a freshly acquired claim and
   * allow both engine paths to start. */
  return read_5xx_owner(&owner) && owner == getpid();
}

void release_5xx_boot(void) {
  pid_t owner = -1;
  if (read_5xx_owner(&owner) && owner == getpid())
    (void)unlink(ONION_5XX_BOOT_OWNER_PATH);
}

class FiveXXBootGuard {
public:
  explicit FiveXXBootGuard(bool required)
      : required_(required), owned_(!required || claim_5xx_boot()) {}
  ~FiveXXBootGuard() {
    if (required_ && owned_)
      release_5xx_boot();
  }
  bool ready() const { return owned_; }

private:
  bool required_;
  bool owned_;
};

} // namespace

int main(void) {
  /*
   * Best-effort file sink: /data/HyperHEN is only created a few lines below,
   * so this first call may not open. It is re-applied once the directory
   * exists; until then records still reach klog, stdout and the remote log.
   */
  onion_log_configure("Bootstrapper", "/data/HyperHEN/HyperHEN.log");
  onion_log_configure_crash("/data/HyperHEN/HyperHEN_crash.log");

  BootstrapConfig config{};
  if (!bootstrap_config_load(&config))
    return -1;

  if (!bootstrap_runtime_prepare())
    return -1;
  bootstrap_runtime_enable_remote_logging();

  const char *conflict = onion_conflict_detect();
  if (conflict) {
    LOG_ERROR("refusing start: %s already running", conflict);
    bootstrap_notify("notify.boot.conflict", conflict);
    return 0;
  }

  LOG_DEBUG("============== Spawner (Bootstrapper) Started =================");
  bootstrap_filesystem_create_directories();
  /* The directory exists now, so this pass is the one that sticks. */
  onion_log_configure("Bootstrapper", "/data/HyperHEN/HyperHEN.log");
  onion_log_configure_crash("/data/HyperHEN/HyperHEN_crash.log");
  if (!bootstrap_filesystem_mount_system())
    return -1;

  LOG_DEBUG("Writing embedded assets ...");
  bootstrap_notify_starting(bootstrap_assets_write());
  LOG_DEBUG("   Written!");

  const bool needs_5xx_selector =
      onion_engine_selector_supported(config.firmware_version);
  FiveXXBootGuard boot_guard(needs_5xx_selector);
  if (!boot_guard.ready()) {
    LOG_ERROR("Another HyperHEN selector bootstrap owns this boot session");
    bootstrap_notify(onion_engine_selector_is_legacy(config.firmware_version)
                         ? "notify.boot.legacy_already_starting"
                         : "notify.boot.5xx_already_starting");
    return -2;
  }

  const int linux_boot =
      bootstrap_linux_boot_if_armed(config.firmware_version);
  if (linux_boot != 0)
    return linux_boot > 0 ? 0 : -2;

  bootstrap_filesystem_disable_updates();

  const Onion5xxBootMode boot_mode_5xx =
      bootstrap_hyperdeck_select_5xx_mode(config.firmware_version);
  if (boot_mode_5xx == ONION_5XX_BOOT_MODE_INVALID)
    return -2;
  if (!bootstrap_hyperdeck_wait_legacy_browser(config.firmware_version))
    return -2;

  const int launch_result = bootstrap_launch_services(
      config.firmware_version, config.kstuff_autoload, boot_mode_5xx);
  if (launch_result < 0)
    return launch_result;

  /* The Media web app is optional and owns no ShellUI hooks. */
  bootstrap_hyperdeck_start(config.firmware_version);

  if (config.hype_voice)
    LOG_DEBUG("Startup voice delegated to the SceShellUI audio context");

  if (launch_result == BOOTSTRAP_LAUNCH_FULL) {
    bootstrap_payload_autostart();
  } else if (launch_result == BOOTSTRAP_LAUNCH_CORE_ONLY) {
    LOG_WARN("Firmware is running the kstuff-lite + Automount route; "
             "payload autostart is disabled");
  } else {
    LOG_WARN("Firmware is running in userland-only compatibility mode; "
             "payload autostart is disabled");
  }
  LOG_DEBUG("============== Spawner (Bootstrapper) Finished =================");
  return 0;
}
