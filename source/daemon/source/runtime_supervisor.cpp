/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Runtime dependency supervisor:
 *   external :9021 (bootstrap/recovery) -> private :9020 -> util
 *
 * User payload requests are deliberately outside this recovery loop. A failed
 * request is never replayed: the caller must retry after :9020 is healthy.
 */

#include "daemon_ops.hpp"

#include <elfldr_remote.h>
#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/system_tmp.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

extern "C" {
extern uint8_t util_elf_start[];
extern const unsigned int util_elf_size;
extern uint8_t hyperhen_elfldr_elf_start[];
extern const unsigned int hyperhen_elfldr_elf_size;
int sceKernelGetProcessName(int pid, char *name);
}

namespace {

constexpr int kLoaderFailureThreshold = 2;
constexpr int kLoaderReadyWaitMs = 10000;
constexpr int kUtilReadyWaitMs = 15000;
/* A normal launch gets the full 120-second client response window. If the
 * loader remains busy beyond this grace period, treat payload_spawn as wedged
 * so :9020 can actually recover instead of deferring forever. */
constexpr uint64_t kLoaderBusyGraceSeconds = 150;
constexpr useconds_t kPollUs = 200 * 1000;
constexpr unsigned kHealthySleepSeconds = 3;
constexpr unsigned kMaxBackoffSeconds = 30;

enum class RecoveryResult {
  Ready,
  Busy,
  Failed,
};

pid_t read_pid_marker(const char *path) {
  if (!path)
    return -1;
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;

  char buf[32] = {};
  const ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return -1;

  char *end = nullptr;
  errno = 0;
  const long value = strtol(buf, &end, 10);
  if (errno != 0 || end == buf || value <= 1 || value > INT_MAX ||
      *end != '\0')
    return -1;
  return static_cast<pid_t>(value);
}

uint64_t monotonic_seconds() {
  struct timespec ts = {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return static_cast<uint64_t>(ts.tv_sec);
}

bool process_has_name(pid_t pid, const char *expected) {
  if (pid <= 1 || !expected || !onion_proc_is_alive(pid))
    return false;
  char name[32] = {};
  return sceKernelGetProcessName(pid, name) == 0 &&
         strcmp(name, expected) == 0;
}

pid_t owned_ready_process_pid(const char *ready_name,
                              const char *expected_process_name) {
  pid_t pid = -1;
  if (onion_ready_read_pid(ready_name, &pid) &&
      process_has_name(pid, expected_process_name))
    return pid;
  onion_ready_clear(ready_name);
  return -1;
}

pid_t private_loader_pid() {
  const pid_t recorded = read_pid_marker(ONION_SYSTEM_TMP_ELFLDR_STATE);
  if (process_has_name(recorded, "hyperhen_elfldr.elf"))
    return recorded;

  if (recorded > 1)
    unlink(ONION_SYSTEM_TMP_ELFLDR_STATE);
  return -1;
}

bool private_loader_busy(pid_t loader_pid) {
  const pid_t busy_pid = read_pid_marker(ONION_SYSTEM_TMP_ELFLDR_BUSY);
  if (busy_pid <= 1)
    return false;
  if (!process_has_name(busy_pid, "hyperhen_elfldr.elf")) {
    unlink(ONION_SYSTEM_TMP_ELFLDR_BUSY);
    return false;
  }
  return loader_pid <= 1 || busy_pid == loader_pid;
}

bool wait_process_exit(pid_t pid, int timeout_ms) {
  for (int waited = 0; waited < timeout_ms; waited += 100) {
    if (!onion_proc_is_alive(pid))
      return true;
    usleep(100 * 1000);
  }
  return !onion_proc_is_alive(pid);
}

bool wait_private_loader_ready(int timeout_ms) {
  for (int waited = 0; waited < timeout_ms; waited += 200) {
    if (g_stack_shutting_down.load(std::memory_order_acquire))
      return false;
    if (elfldr_remote_onion_available()) {
      const pid_t pid = private_loader_pid();
      if (pid > 1) {
        LOG_INFO("runtime supervisor: private elfldr ready pid=%d", (int)pid);
        return true;
      }
    }
    usleep(kPollUs);
  }
  return false;
}

RecoveryResult recover_private_loader(bool recover_timed_out_busy_loader) {
  if (elfldr_remote_onion_available())
    return RecoveryResult::Ready;

  const pid_t old_pid = private_loader_pid();
  if (private_loader_busy(old_pid) && !recover_timed_out_busy_loader) {
    LOG_INFO("runtime supervisor: private elfldr pid=%d is busy; defer recovery",
                 (int)old_pid);
    return RecoveryResult::Busy;
  }

  if (old_pid > 1) {
    LOG_INFO("runtime supervisor: stopping unresponsive private elfldr pid=%d",
                 (int)old_pid);
    if (kill(old_pid, SIGKILL) != 0 && errno != ESRCH) {
      LOG_ERROR("runtime supervisor: kill(%d) failed: %s", (int)old_pid,
                   strerror(errno));
      return RecoveryResult::Failed;
    }
    if (!wait_process_exit(old_pid, 3000)) {
      LOG_INFO("runtime supervisor: private elfldr pid=%d did not exit",
                   (int)old_pid);
      return RecoveryResult::Failed;
    }
  }

  unlink(ONION_SYSTEM_TMP_ELFLDR_STATE);
  unlink(ONION_SYSTEM_TMP_ELFLDR_BUSY);

  if (!elfldr_remote_available()) {
    LOG_WARN("runtime supervisor: external recovery elfldr :9021 unavailable");
    return RecoveryResult::Failed;
  }

  LOG_INFO("runtime supervisor: launching embedded private elfldr via :9021");
  if (!elfldr_remote_send_bytes_to(ELFLDR_REMOTE_PORT,
                                   hyperhen_elfldr_elf_start,
                                   hyperhen_elfldr_elf_size)) {
    LOG_ERROR("runtime supervisor: failed to send private elfldr to :9021");
    return RecoveryResult::Failed;
  }

  return wait_private_loader_ready(kLoaderReadyWaitMs) ? RecoveryResult::Ready
                                                       : RecoveryResult::Failed;
}

bool util_running() {
  return owned_ready_process_pid(ONION_READY_UTIL, "hyperhen_util.elf") > 1;
}

bool restart_util_via_private_loader() {
  if (!elfldr_remote_onion_available())
    return false;

  onion_ready_clear(ONION_READY_UTIL);
  if (!elfldr_remote_send_bytes_to(ONION_ELFLDR_PORT, util_elf_start,
                                   util_elf_size)) {
    return false;
  }

  for (int waited = 0; waited < kUtilReadyWaitMs; waited += 200) {
    if (g_stack_shutting_down.load(std::memory_order_acquire))
      return false;
    if (util_running() && onion_ready_is_set(ONION_READY_UTIL))
      return true;
    usleep(kPollUs);
  }
  return false;
}

unsigned recovery_backoff(int failures) {
  const unsigned seconds = static_cast<unsigned>(std::max(failures, 1) * 5);
  return std::min(seconds, kMaxBackoffSeconds);
}

} // namespace

pid_t runtime_owned_util_pid() {
  return owned_ready_process_pid(ONION_READY_UTIL, "hyperhen_util.elf");
}

pid_t runtime_owned_private_loader_pid() { return private_loader_pid(); }

void *runtime_supervisor_thread(void *args) noexcept {
  (void)args;

  int loader_check_failures = 0;
  int loader_recovery_failures = 0;
  int util_recovery_failures = 0;
  bool loader_outage_notified = false;
  bool util_outage_notified = false;
  uint64_t loader_busy_since = 0;

  LOG_INFO("runtime supervisor started (:9021 recovery -> :9020 -> util)");

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    if (!elfldr_remote_onion_available()) {
      const pid_t pid = private_loader_pid();
      const bool busy = private_loader_busy(pid);
      bool busy_timed_out = false;
      if (busy) {
        const uint64_t now = monotonic_seconds();
        if (loader_busy_since == 0)
          loader_busy_since = now;
        busy_timed_out = now != 0 && loader_busy_since != 0 &&
                         now - loader_busy_since >= kLoaderBusyGraceSeconds;
      } else {
        loader_busy_since = 0;
      }

      if (busy && !busy_timed_out) {
        loader_check_failures = 0;
        sleep(1);
        continue;
      }

      if (busy_timed_out) {
        LOG_INFO("runtime supervisor: private elfldr busy for at least %llu seconds; forcing recovery",
                     (unsigned long long)kLoaderBusyGraceSeconds);
      }

      if (++loader_check_failures < kLoaderFailureThreshold) {
        sleep(1);
        continue;
      }

      if (!loader_outage_notified) {
        onion_notify(true,
                     "notify.loader.recovering");
        loader_outage_notified = true;
      }

      const RecoveryResult recovered = recover_private_loader(busy_timed_out);
      if (recovered == RecoveryResult::Ready) {
        onion_notify(true, "notify.loader.recovered");
        loader_check_failures = 0;
        loader_recovery_failures = 0;
        loader_outage_notified = false;
        loader_busy_since = 0;
      } else if (recovered == RecoveryResult::Busy) {
        loader_check_failures = 0;
        sleep(1);
        continue;
      } else {
        ++loader_recovery_failures;
        LOG_ERROR("runtime supervisor: loader recovery failed (%d)",
                     loader_recovery_failures);
        if (loader_recovery_failures == 3) {
          onion_notify(true,
                       "notify.loader.recover_failed");
        }
        sleep(recovery_backoff(loader_recovery_failures));
        continue;
      }
    } else {
      loader_check_failures = 0;
      loader_recovery_failures = 0;
      loader_outage_notified = false;
      loader_busy_since = 0;
    }

    if (!util_running()) {
      if (!util_outage_notified) {
        onion_notify(true, "notify.util.restarting");
        util_outage_notified = true;
      }

      if (restart_util_via_private_loader()) {
        LOG_INFO("runtime supervisor: util recovered through private :9020");
        onion_notify(true, "notify.util.restarted");
        util_recovery_failures = 0;
        util_outage_notified = false;
      } else {
        ++util_recovery_failures;
        LOG_ERROR("runtime supervisor: util recovery failed (%d)",
                     util_recovery_failures);
        if (util_recovery_failures == 3) {
          onion_notify(true,
                       "notify.util.restart_failed");
        }
        sleep(recovery_backoff(util_recovery_failures));
        continue;
      }
    } else {
      util_recovery_failures = 0;
      util_outage_notified = false;
    }

    sleep(kHealthySleepSeconds);
  }

  LOG_INFO("runtime supervisor stopped (stack shutdown)");
  return nullptr;
}
