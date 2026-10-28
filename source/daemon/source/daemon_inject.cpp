/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Toolbox injection for explicit commands and SceShellUI lifecycle recovery.
 */

#include "daemon_ops.hpp"
#include "daemon_power_state.hpp"
#include "globalconf.hpp"
#include "toolbox_injection.hpp"
#include "toolbox_webkit_gate.hpp"
#include "toolbox_webkit_snapshot.hpp"
#include "freebsd-helper.h"
#include <onion/boot_mode_5xx.h>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/settings.hpp>
#include <ps5/kernel.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

extern "C" {
bool Inject_Toolbox(int pid, uint8_t *elf);
bool Inject_Toolbox_Guarded(int pid, uint8_t *elf, bool (*pre_attach)(int));
extern uint8_t shellui_elf_start[];
}

namespace {

onion::ToolboxInject g_toolbox_inject;
std::mutex g_toolbox_flow_mutex;
std::atomic<pid_t> g_last_shellui{0};
std::atomic<uint32_t> g_rest_gen{0};

struct RestInjectJob {
  pid_t exec_pid;
  pid_t previous_pid;
  uint32_t gen;
};

pid_t toolbox_live_pid() {
  return static_cast<pid_t>(get_shellui_pid());
}

bool toolbox_already_ready(pid_t pid) {
  return pid > 0 && g_toolbox_inject.is_ready_for(pid);
}

void toolbox_remember_pid(pid_t pid) {
  if (pid > 1) {
    g_last_shellui.store(pid, std::memory_order_release);
  }
}

/*
 * Same gate kstuff uses before touching a just-exec'd NPXS40087: userland
 * libs are mapped.  Each name is 60 x 500ms (ps5-kstuff-ldr/src/shellui_patch.c).
 * Poll is on a known pid inside the rest worker, not a process scanner.
 */
constexpr const char *kShellUiReadySprx[] = {
    "libSceNpTrophy.sprx",
    "libSceNpTrophy2.sprx",
};
constexpr useconds_t kSprxPollUs = 500 * 1000;
constexpr int kSprxPollMax = 60;
constexpr int kRestRetryMax = 4;
constexpr useconds_t kRestRetryDelayUs[] = {
    500 * 1000,
    1000 * 1000,
    2000 * 1000,
};
constexpr int kColdResolvePollMax = 20;
constexpr int kColdSprxPollMax = 40;
constexpr int kColdQuietPolls = 4;
constexpr useconds_t kColdQuietPollUs = 250 * 1000;
constexpr useconds_t kWebkitPollUs = 250 * 1000;
constexpr size_t kWebkitSnapshotMaxBytes = 32u * 1024u * 1024u;
constexpr int kWebkitSnapshotAttempts = 3;

bool toolbox_is_5xx_selector_boot() {
  const Onion5xxBootMode mode =
      onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
  return mode == ONION_5XX_BOOT_MODE_BYPERVISOR ||
         mode == ONION_5XX_BOOT_MODE_HYPERCORE;
}

bool toolbox_clear_5xx_selector_marker() {
  if (unlink(ONION_5XX_SELECTOR_ACTIVE_PATH) == 0 || errno == ENOENT)
    return true;
  LOG_WARN("5.xx: cannot clear selector-active marker errno=%d", errno);
  return false;
}

/* A normal WebKit payload host also owns ShellUI state, on every firmware.
 * Observe only the web content process: UI/network helper processes may
 * persist after the user leaves the browser and are not blockers. */
using onion::ToolboxWebkitSnapshot;
static_assert(offsetof(kinfo_proc, ki_pid) == 72,
              "WebKit query must use the existing PS5 process layout");
static_assert(offsetof(kinfo_proc, ki_tdname) == 394,
              "WebKit query must use the existing PS5 process layout");
static_assert(offsetof(kinfo_proc, ki_comm) == 447,
              "WebKit query must use the existing PS5 process layout");

bool toolbox_webkit_snapshot(ToolboxWebkitSnapshot *out) {
  if (!out) { errno = EINVAL; return false; }
  for (int attempt = 0; attempt < kWebkitSnapshotAttempts; ++attempt) {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0)
      return false;
    if (size == 0 || size > kWebkitSnapshotMaxBytes) {
      errno = EOVERFLOW;
      return false;
    }
    void *buffer = std::malloc(size);
    if (!buffer) { errno = ENOMEM; return false; }
    const size_t capacity = size;
    if (sysctl(mib, 4, buffer, &size, nullptr, 0) != 0) {
      const int error = errno;
      std::free(buffer);
      if (error == ENOMEM && attempt + 1 < kWebkitSnapshotAttempts)
        continue;
      errno = error;
      return false;
    }
    ToolboxWebkitSnapshot snapshot;
    constexpr onion::ToolboxProcessLayout layout{
        offsetof(kinfo_proc, ki_pid), offsetof(kinfo_proc, ki_comm),
        sizeof(((kinfo_proc *)nullptr)->ki_comm),
        offsetof(kinfo_proc, ki_tdname),
        sizeof(((kinfo_proc *)nullptr)->ki_tdname)};
    const bool valid = size > 0 && size <= capacity &&
        onion::toolbox_parse_webkit_snapshot(buffer, size, layout, &snapshot);
    std::free(buffer);
    if (!valid) { errno = EPROTO; return false; }
    *out = snapshot;
    errno = 0;
    return true;
  }
  errno = EAGAIN;
  return false;
}

bool toolbox_monotonic_ms(uint64_t *out) {
  timespec now{};
  if (!out || clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
      static_cast<uint64_t>(now.tv_sec) > UINT64_MAX / 1000u)
    return false;
  *out = static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
  return true;
}

bool toolbox_wait_webkit_exit(pid_t target_pid, uint32_t generation) {
  uint64_t started = 0;
  if (!toolbox_monotonic_ms(&started)) {
    LOG_WARN("Toolbox R29 WebKit gate: clock unavailable; skipping injection");
    return false;
  }
  onion::ToolboxWebkitGate gate(static_cast<int>(target_pid), started);
  pid_t previous_web_pid = -1;
  for (;;) {
    if (g_rest_gen.load(std::memory_order_acquire) != generation) {
      LOG_INFO("Toolbox R29 WebKit gate superseded pid=%d", static_cast<int>(target_pid));
      return false;
    }
    ToolboxWebkitSnapshot snapshot;
    const bool query_ok = toolbox_webkit_snapshot(&snapshot);
    const int query_error = query_ok ? 0 : errno;
    uint64_t now = 0;
    if (!toolbox_monotonic_ms(&now)) {
      LOG_WARN("Toolbox R29 WebKit gate: clock unavailable; skipping injection");
      return false;
    }
    const auto decision = gate.observe(now, query_ok,
        static_cast<int>(snapshot.shellui_pid), static_cast<int>(snapshot.web_pid));
    if (decision == onion::ToolboxWebkitDecision::Ready) {
      LOG_INFO("Toolbox R29 WebKit gate ready: pid=%d, browser absent for 2000 ms",
               static_cast<int>(target_pid));
      if (toolbox_is_5xx_selector_boot() &&
          access(ONION_5XX_SELECTOR_ACTIVE_PATH, F_OK) == 0)
        return toolbox_clear_5xx_selector_marker();
      return true;
    }
    if (decision != onion::ToolboxWebkitDecision::Waiting) {
      LOG_WARN("Toolbox R29 WebKit gate stopped: pid=%d decision=%d errno=%d",
               static_cast<int>(target_pid), static_cast<int>(decision), query_error);
      return false;
    }
    if (snapshot.web_pid > 1 && snapshot.web_pid != previous_web_pid) {
      LOG_INFO("Toolbox R29 waiting for browser pid=%d to exit manually; target=%d",
               static_cast<int>(snapshot.web_pid), static_cast<int>(target_pid));
    }
    previous_web_pid = snapshot.web_pid;
    usleep(kWebkitPollUs);
  }
}

bool toolbox_webkit_pre_attach(int target_pid) {
  ToolboxWebkitSnapshot snapshot;
  const bool allowed = onion::toolbox_pre_attach_allowed(
      target_pid, [](ToolboxWebkitSnapshot *out) { return toolbox_webkit_snapshot(out); },
      &snapshot);
  if (!allowed) {
    LOG_WARN("Toolbox R29 pre-attach cancelled: expected=%d shellui=%d web=%d errno=%d",
             target_pid, static_cast<int>(snapshot.shellui_pid),
             static_cast<int>(snapshot.web_pid), errno);
    return false;
  }
  return true;
}

bool toolbox_sprx_loaded(pid_t pid, const char *name) {
  uint32_t handle = static_cast<uint32_t>(-1);
  return kernel_dynlib_handle(pid, name, &handle) == 0 &&
         handle != static_cast<uint32_t>(-1);
}

bool toolbox_wait_one_sprx(pid_t pid, uint32_t gen, const char *name) {
  for (int i = 0; i < kSprxPollMax; ++i) {
    if (g_rest_gen.load(std::memory_order_acquire) != gen) {
      LOG_DEBUG("rest: sprx wait superseded gen=%u pid=%d name=%s", gen,
                static_cast<int>(pid), name);
      return false;
    }
    if (!isProcessAlive(pid)) {
      LOG_DEBUG("rest: sprx wait pid=%d died at try=%d name=%s",
                static_cast<int>(pid), i, name);
      return false;
    }
    if (toolbox_sprx_loaded(pid, name)) {
      LOG_DEBUG("rest: %s loaded pid=%d after %d ms", name,
                static_cast<int>(pid), i * 500);
      return true;
    }
    usleep(kSprxPollUs);
  }
  LOG_DEBUG("rest: %s still missing pid=%d after %d ms", name,
            static_cast<int>(pid), kSprxPollMax * 500);
  return false;
}

bool toolbox_wait_shellui_sprx(pid_t pid, uint32_t gen) {
  for (const char *name : kShellUiReadySprx) {
    if (!toolbox_wait_one_sprx(pid, gen, name)) {
      return false;
    }
  }
  return true;
}

bool toolbox_wait_resume_delay(pid_t pid, uint32_t gen) {
  uint64_t delay = g_settings.snapshot().rest_mode_delay_seconds;
  if (delay > static_cast<uint64_t>(onion::kRestModeDelayMaxSeconds))
    delay = onion::kRestModeDelayMaxSeconds;
  if (delay == 0)
    return true;

  LOG_DEBUG("rest: resume delay %llu s after trophy sprx pid=%d",
            static_cast<unsigned long long>(delay), static_cast<int>(pid));
  for (uint64_t i = 0; i < delay; ++i) {
    if (g_rest_gen.load(std::memory_order_acquire) != gen) {
      LOG_DEBUG("rest: resume delay superseded gen=%u pid=%d", gen,
                static_cast<int>(pid));
      return false;
    }
    if (!isProcessAlive(pid)) {
      LOG_DEBUG("rest: resume delay pid=%d died at %llu/%llu s",
                static_cast<int>(pid), static_cast<unsigned long long>(i),
                static_cast<unsigned long long>(delay));
      return false;
    }
    sleep(1);
  }
  return g_rest_gen.load(std::memory_order_acquire) == gen &&
         isProcessAlive(pid);
}

/*
 * The initial daemon path used to inject as soon as it found SceShellUI.
 * On 5.xx that can overlap the selector WebView teardown and the loader's
 * first ShellUI patch.  Require the same ready SPRX set as recovery, then a
 * short unchanged-PID window before ptrace touches the process.
 */
bool toolbox_wait_cold_target(pid_t expected_pid, pid_t *ready_pid) {
  if (!ready_pid)
    return false;

  for (int retry = 0; retry < kRestRetryMax; ++retry) {
    pid_t pid = expected_pid;
    for (int resolve = 0; pid <= 1 && resolve < kColdResolvePollMax;
         ++resolve) {
      pid = toolbox_live_pid();
      if (pid <= 1)
        usleep(kColdQuietPollUs);
    }
    if (pid <= 1)
      return false;

    bool sprx_ready = false;
    for (int poll = 0; poll < kColdSprxPollMax; ++poll) {
      if (!isProcessAlive(pid) || toolbox_live_pid() != pid)
        break;
      if (toolbox_sprx_loaded(pid, kShellUiReadySprx[0]) &&
          toolbox_sprx_loaded(pid, kShellUiReadySprx[1])) {
        sprx_ready = true;
        break;
      }
      usleep(kSprxPollUs);
    }
    if (!sprx_ready) {
      if (expected_pid > 1)
        return false;
      continue;
    }

    bool stable = true;
    for (int poll = 0; poll < kColdQuietPolls; ++poll) {
      usleep(kColdQuietPollUs);
      if (!isProcessAlive(pid) || toolbox_live_pid() != pid) {
        stable = false;
        break;
      }
    }
    if (stable) {
      *ready_pid = pid;
      return true;
    }
    if (expected_pid > 1)
      return false;
  }
  return false;
}

void toolbox_wait_kstuff() {
  const Onion5xxBootMode boot_mode =
      onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
  const char *ready_marker =
      boot_mode == ONION_5XX_BOOT_MODE_BYPERVISOR
          ? ONION_READY_HYPERVISOR_5XX
          : ONION_READY_KSTUFF;
  if (!onion_ready_wait(ready_marker, /*timeout_ms=*/5000,
                        /*poll_ms=*/200))
    sleep(1);
}

void toolbox_notify_outcome(const onion::ToolboxInjectionOutcome &outcome) {
  switch (outcome.result) {
  case onion::ToolboxInjectionResult::AlreadyReady:
    LOG_WARN("Toolbox already active in SceShellUI pid=%d",
             static_cast<int>(outcome.pid));
    LOG_DEBUG("rest: skip inject, ready marker matches pid=%d",
              static_cast<int>(outcome.pid));
    break;
  case onion::ToolboxInjectionResult::Injected:
    LOG_INFO("Toolbox online for SceShellUI pid=%d (ready protocol)",
             static_cast<int>(outcome.pid));
    LOG_DEBUG("rest: ensure Injected pid=%d", static_cast<int>(outcome.pid));
    break;
  case onion::ToolboxInjectionResult::TargetNotFound:
    LOG_DEBUG("rest: ensure TargetNotFound");
    onion_notify(true, "notify.toolbox.no_shellui_pid");
    break;
  case onion::ToolboxInjectionResult::InjectFailed:
    LOG_DEBUG("rest: ensure InjectFailed pid=%d",
              static_cast<int>(outcome.pid));
    onion_notify(true, "notify.toolbox.inject_failed");
    break;
  case onion::ToolboxInjectionResult::ReadyTimeout:
    LOG_DEBUG("rest: ensure ReadyTimeout pid=%d",
              static_cast<int>(outcome.pid));
    onion_notify(true, "notify.toolbox.load_timeout");
    break;
  case onion::ToolboxInjectionResult::TargetChanged:
    LOG_DEBUG("rest: ensure TargetChanged pid=%d",
              static_cast<int>(outcome.pid));
    onion_notify(true, "notify.toolbox.shellui_restarted");
    break;
  }
}

bool toolbox_inject_immediate(pid_t expected_pid = 0) {
  std::lock_guard<std::mutex> flow_lock(g_toolbox_flow_mutex);
  const pid_t observed_pid = expected_pid > 1 ? expected_pid : toolbox_live_pid();
  LOG_DEBUG("rest: cmd_enable_toolbox shellui_pid=%d",
            static_cast<int>(observed_pid));
  if (toolbox_already_ready(observed_pid) &&
      observed_pid == toolbox_live_pid()) {
    toolbox_remember_pid(observed_pid);
    toolbox_notify_outcome({onion::ToolboxInjectionResult::AlreadyReady,
                            observed_pid});
    return true;
  }

  toolbox_wait_kstuff();
  pid_t target_pid = 0;
  if (!toolbox_wait_cold_target(expected_pid, &target_pid)) {
    const pid_t current_pid = toolbox_live_pid();
    const onion::ToolboxInjectionResult result =
        observed_pid > 1 && current_pid > 1 && current_pid != observed_pid
            ? onion::ToolboxInjectionResult::TargetChanged
            : onion::ToolboxInjectionResult::TargetNotFound;
    toolbox_notify_outcome({result, observed_pid});
    return false;
  }
  if (toolbox_already_ready(target_pid)) {
    toolbox_remember_pid(target_pid);
    toolbox_notify_outcome({onion::ToolboxInjectionResult::AlreadyReady,
                            target_pid});
    return true;
  }
  const uint32_t generation = g_rest_gen.load(std::memory_order_acquire);
  if (!toolbox_wait_webkit_exit(target_pid, generation))
    return false;
  LOG_INFO("Activating toolbox...");

  const onion::ToolboxInjectionOutcome outcome = g_toolbox_inject.inject(
      []() -> pid_t { return toolbox_live_pid(); },
      [](pid_t pid) -> bool {
        LOG_DEBUG("Injecting toolbox into SceShellUI pid=%d",
                  static_cast<int>(pid));
        return Inject_Toolbox_Guarded(static_cast<int>(pid), shellui_elf_start,
                                     toolbox_webkit_pre_attach);
      },
      /*timeout_ms=*/45 * 1000, /*poll_ms=*/250, target_pid);

  toolbox_remember_pid(outcome.pid);
  toolbox_notify_outcome(outcome);
  return outcome.ready();
}

bool toolbox_inject_rest(pid_t exec_pid, pid_t previous_pid, uint32_t gen,
                         bool apply_user_delay) {
  LOG_DEBUG("rest: SysCore EXEC shellui pid=%d previous=%d gen=%u",
            static_cast<int>(exec_pid), static_cast<int>(previous_pid), gen);
  const bool replacement = previous_pid > 1 && previous_pid != exec_pid;
  LOG_DEBUG("rest: shellui pid %d -> %d, wait trophy sprx",
            static_cast<int>(previous_pid), static_cast<int>(exec_pid));
  if (!toolbox_wait_shellui_sprx(exec_pid, gen)) {
    LOG_DEBUG("rest: skip inject, pid=%d not ready for ELF load",
              static_cast<int>(exec_pid));
    return false;
  }
  if (apply_user_delay && !toolbox_wait_resume_delay(exec_pid, gen)) {
    LOG_DEBUG("rest: skip inject after resume delay pid=%d",
              static_cast<int>(exec_pid));
    return false;
  }
  if (g_rest_gen.load(std::memory_order_acquire) != gen) {
    LOG_DEBUG("rest: inject superseded after sprx wait gen=%u pid=%d", gen,
              static_cast<int>(exec_pid));
    return false;
  }
  if (replacement) {
    onion_notify(true, "notify.rest.reactivating");
  }
  return toolbox_inject_immediate(exec_pid);
}

void *toolbox_rest_worker(void *arg) {
  RestInjectJob job = *static_cast<RestInjectJob *>(arg);
  delete static_cast<RestInjectJob *>(arg);
  for (int attempt = 0; attempt < kRestRetryMax; ++attempt) {
    if (g_rest_gen.load(std::memory_order_acquire) != job.gen) {
      LOG_DEBUG("rest: inject retry superseded gen=%u", job.gen);
      break;
    }
    pid_t target = job.exec_pid;
    if (target > 1 &&
        (!isProcessAlive(target) || toolbox_live_pid() != target)) {
      LOG_DEBUG("rest: observed ShellUI pid changed from %d",
                static_cast<int>(target));
      target = 0;
    }
    if (target <= 1) {
      for (int resolve_try = 0; resolve_try < 30 && target <= 1;
           ++resolve_try) {
        target = toolbox_live_pid();
        if (target <= 1) {
          usleep(1000 * 1000);
        }
      }
    }
    if (target > 1 &&
        toolbox_inject_rest(target, job.previous_pid, job.gen,
                            /*apply_user_delay=*/attempt == 0)) {
      toolbox_remember_pid(target);
      return nullptr;
    }
    if (attempt + 1 < kRestRetryMax) {
      LOG_DEBUG("rest: inject retry=%d/%d target=%d", attempt + 1,
                kRestRetryMax - 1, static_cast<int>(target));
      usleep(kRestRetryDelayUs[attempt]);
    }
  }
  LOG_WARN("rest: toolbox recovery exhausted gen=%u requested_pid=%d", job.gen,
           static_cast<int>(job.exec_pid));
  return nullptr;
}

void start_rest_inject(pid_t exec_pid) {
  if (exec_pid > 1 && daemon_power_state_is_sleeping()) {
    toolbox_remember_pid(exec_pid);
    LOG_INFO("rest: defer ShellUI injection during standby pid=%d",
             static_cast<int>(exec_pid));
    return;
  }
  if (exec_pid <= 1 && toolbox_live_pid() <= 1) {
    LOG_DEBUG("rest: no SceShellUI pid yet; compensation worker will retry");
  }
  const pid_t previous = exec_pid > 1
                             ? g_last_shellui.exchange(exec_pid,
                                                       std::memory_order_acq_rel)
                             : g_last_shellui.load(std::memory_order_acquire);
  const uint32_t gen =
      g_rest_gen.fetch_add(1, std::memory_order_acq_rel) + 1;
  auto *job = new RestInjectJob{exec_pid, previous, gen};
  pthread_t thread = nullptr;
  if (pthread_create(&thread, nullptr, toolbox_rest_worker, job) != 0) {
    LOG_ERROR("rest: pthread_create for shellui exec pid=%d failed",
              static_cast<int>(exec_pid));
    delete job;
    (void)toolbox_inject_rest(exec_pid, previous, gen,
                              /*apply_user_delay=*/true);
    return;
  }
  pthread_detach(thread);
}

} // namespace

void toolbox_on_new_shellui(pid_t pid) {
  /* NOTE_EXEC denotes a new image, even when the kernel reuses the PID. */
  onion_ready_clear(ONION_READY_TOOLBOX);
  if (daemon_power_state_is_sleeping()) {
    toolbox_remember_pid(pid);
    LOG_INFO("rest: NOTE_EXEC during standby; defer ShellUI injection pid=%d",
             static_cast<int>(pid));
    return;
  }
  start_rest_inject(pid);
}

void toolbox_on_resume() {
  if (daemon_power_state_is_sleeping()) {
    LOG_DEBUG("rest: resume compensation still in standby; defer");
    return;
  }
  const pid_t pid = toolbox_live_pid();
  if (pid <= 1) {
    start_rest_inject(0);
    return;
  }
  if (toolbox_already_ready(pid)) {
    toolbox_remember_pid(pid);
    LOG_DEBUG("rest: resume compensation sees ready toolbox pid=%d",
              static_cast<int>(pid));
    return;
  }
  LOG_INFO("rest: resume compensation scheduling ShellUI pid=%d",
           static_cast<int>(pid));
  start_rest_inject(pid);
}

bool cmd_enable_toolbox() {
  return toolbox_inject_immediate();
}
