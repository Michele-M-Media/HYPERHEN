#include "hyper_runtime.hpp"
#include "hyper_worker_launch.hpp"
#include "hyper_ready_wait.hpp"
#include "hyper_audio_gate.hpp"
#include "hyper_selection_state.hpp"
#include "daemon_ops.hpp"
#include "daemon_power_state.hpp"
#include "freebsd-helper.h"
#include "onion_cjson.hpp"
#if defined(HYPERHEN_INTEGRATED_HYPER)
#include "hyper_installer.hpp"
#endif

#include <elfldr_remote.h>
#include <onion/hyper_assets.hpp>
#include <onion/hyper_lifecycle.hpp>
#include <onion/hyper_runtime_selection.hpp>
#include <onion/hyper_voice_catalog.hpp>
#include <onion/hyper_model_catalog.hpp>
#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>
#include <onion/ready.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

extern "C" {
extern uint8_t hyper_assistant_elf_start[];
extern const unsigned int hyper_assistant_elf_size;
int sceKernelGetProcessName(int pid, char *name);
}

namespace onion::daemon::hyper {
namespace {
namespace wire = onion::hyper;
constexpr unsigned kPollUs = 200000;
constexpr unsigned kReadyPolls = 600; // STT initialization retains P14's 120 s cap.
constexpr unsigned kStopPolls = 100; // Cooperative cancellation normally finishes promptly.

struct ProcessIdentity {
  pid_t pid = -1;
  uint64_t token = 0;
  timeval started{};
  bool have_start = false;
  int model_id = 0;
};

std::mutex mutex;
std::mutex stop_mutex;
AudioGate marker_gate;
SelectionState selected;
wire::LaunchPolicy policy;
std::atomic<bool> enabled{true};
std::atomic<bool> running{false};
std::atomic<bool> quit{false};
std::atomic<unsigned> verification_epoch{1};
pthread_t thread = nullptr;
ProcessIdentity owned;
bool cached_assets = false;
const char *state = "waiting_toolbox";

uint64_t monotonic_token() {
  timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return 0;
  return (static_cast<uint64_t>(now.tv_sec) * 1000000000u +
          static_cast<uint64_t>(now.tv_nsec)) ^
         (static_cast<uint64_t>(getpid()) << 32u);
}

bool wants_on() {
  return enabled.load(std::memory_order_acquire) &&
         !quit.load(std::memory_order_acquire) &&
         !g_stack_shutting_down.load(std::memory_order_acquire);
}

void set_state(const char *value) {
  std::lock_guard<std::mutex> guard(mutex);
  state = value;
}

void installation_started() {
  std::lock_guard<std::mutex> guard(mutex);
  cached_assets = false;
  policy.assets_changed(false);
  state = "missing";
}

bool audio_gate(bool disabled, int expected_model = -1) {
  return marker_gate.apply(disabled, [expected_model]() {
        return wants_on() && (expected_model < 0 || selection().model_id == expected_model);
      },
      []() {
        const int fd = open(wire::kDisabledMarker,
                            O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        const bool written = write(fd, "1", 1) == 1;
        (void)close(fd);
        return written;
      },
      []() { return unlink(wire::kDisabledMarker) == 0 || errno == ENOENT; });
}

bool read_marker(wire::WorkerMarker *marker) {
  if (!marker) return false;
  const int fd = open(wire::kWorkerMarker, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st{};
  char buffer[128]{};
  const bool regular = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
                       st.st_size > 0 && st.st_size < static_cast<off_t>(sizeof(buffer));
  const ssize_t got = regular ? read(fd, buffer, sizeof(buffer) - 1) : -1;
  (void)close(fd);
  return got > 0 && got == st.st_size && static_cast<size_t>(got) == std::strlen(buffer) &&
         wire::parse_worker_marker(buffer, marker);
}

void clear_our_marker(const ProcessIdentity &process) {
  wire::WorkerMarker marker;
  if (read_marker(&marker) && wire::worker_marker_matches(marker, process.pid, process.token))
    (void)unlink(wire::kWorkerMarker);
}

bool read_process_start(pid_t pid, timeval *started) {
  if (!started || pid <= 1) return false;
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid)};
  kinfo_proc info{};
  size_t size = sizeof(info);
  if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 ||
      size < offsetof(kinfo_proc, ki_start) + sizeof(info.ki_start) ||
      info.ki_pid != pid || info.ki_start.tv_sec <= 0) return false;
  *started = info.ki_start;
  return true;
}

// The exact loader PID and birth time identify a process during initialization;
// after READY, its launch token marker provides an additional identity check.
bool same_process(const ProcessIdentity &process) {
  if (process.pid <= 1) return false;
  timeval current{};
  char name[64]{};
  if (sceKernelGetProcessName(process.pid, name) != 0 ||
      std::strcmp(name, "SceSpZeroConf") != 0) return false;
  if (process.have_start)
    return read_process_start(process.pid, &current) &&
           current.tv_sec == process.started.tv_sec &&
           current.tv_usec == process.started.tv_usec;
  wire::WorkerMarker marker;
  return read_marker(&marker) &&
         wire::worker_marker_matches(marker, process.pid, process.token);
}

ProcessIdentity current_owned() {
  std::lock_guard<std::mutex> guard(mutex);
  return owned;
}

void stop_owned() {
  std::lock_guard<std::mutex> stop_guard(stop_mutex);
  ProcessIdentity process = current_owned();
  if (process.pid <= 1) return;
  audio_gate(true);
  set_state("stopping");
  if (!process.have_start && !same_process(process) && onion_proc_is_alive(process.pid)) {
    // Never signal an unverified PID. A late READY marker can establish the
    // launch token even if the initial kernel birth-time query was unavailable.
    for (unsigned i = 0; i < kStopPolls && onion_proc_is_alive(process.pid); ++i) {
      if (same_process(process)) break;
      usleep(100000);
    }
    if (onion_proc_is_alive(process.pid) && !same_process(process)) {
      LOG_ERROR("Hyper R45: cannot verify owned worker pid=%d; no signal sent", static_cast<int>(process.pid));
      set_state("error");
      return;
    }
  }
  if (same_process(process)) {
    LOG_INFO("Hyper R45: cooperative stop pid=%d", static_cast<int>(process.pid));
    if (kill(process.pid, SIGTERM) != 0 && errno != ESRCH)
      LOG_WARN("Hyper R45: SIGTERM failed pid=%d errno=%d", static_cast<int>(process.pid), errno);
    for (unsigned i = 0; i < kStopPolls && same_process(process); ++i)
      usleep(100000);
    // A wedged native call cannot be allowed to retain the worker allocation.
    // This fallback is restricted to the exact process instance we launched.
    if (same_process(process)) {
      LOG_WARN("Hyper R45: worker stop timed out pid=%d", static_cast<int>(process.pid));
      (void)kill(process.pid, SIGKILL);
      for (unsigned i = 0; i < 20 && same_process(process); ++i) usleep(100000);
    }
  }
  if (same_process(process)) { set_state("error"); return; }
  clear_our_marker(process);
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (owned.pid == process.pid && owned.token == process.token) owned = {};
    state = wants_on() ? "waiting_toolbox" : "off";
  }
  onion_notify(true, "notify.hyper.stopped");
}

bool worker_lock_available() {
  const int fd = open(wire::kWorkerLock, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  const bool available = flock(fd, LOCK_EX | LOCK_NB) == 0;
  if (available) (void)flock(fd, LOCK_UN);
  (void)close(fd);
  return available;
}

bool toolbox_is_current(int pid) {
  return pid > 1 && get_shellui_pid() == pid &&
         onion_ready_matches_pid(ONION_READY_TOOLBOX, static_cast<pid_t>(pid));
}

void start_worker(int toolbox_pid, int model_id) {
  if (!wants_on() || daemon_power_state_is_sleeping() ||
      selection().model_id != model_id || !toolbox_is_current(toolbox_pid) ||
      !wire::assets_ready_for_model(model_id)) return;
  if (!elfldr_remote_onion_available()) return; // Dependency may still be recovering.
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (!wants_on() || selection().model_id != model_id ||
        policy.toolbox_pid() != toolbox_pid || owned.pid > 1) return;
    state = "starting";
  }
  bool lock_available = false;
  for (unsigned i = 0; i < 5; ++i) {
    if (!wants_on() || daemon_power_state_is_sleeping() ||
        selection().model_id != model_id || !toolbox_is_current(toolbox_pid)) return;
    if (wire::install_in_progress()) { installation_started(); return; }
    if (worker_lock_available()) { lock_available = true; break; }
    // The installer removes .installing immediately before its guard releases
    // flock. Allow that bounded handoff without consuming the start event.
    if (i + 1 < 5) usleep(100000);
  }
  if (!lock_available) {
    LOG_WARN("Hyper R45: existing Hyper worker owns the singleton lock; no adoption or retry");
    {
      std::lock_guard<std::mutex> guard(mutex);
      policy.attempted();
      state = "error";
    }
    onion_notify(true, "notify.hyper.start_failed");
    return;
  }
  // Recheck settings after every potentially blocking dependency check.
  if (!wants_on() || daemon_power_state_is_sleeping() ||
      selection().model_id != model_id || !toolbox_is_current(toolbox_pid)) return;
  if (!wire::assets_ready_for_model(model_id)) { installation_started(); return; }
  {
    std::lock_guard<std::mutex> guard(mutex);
    if (!wants_on() || selection().model_id != model_id ||
        policy.toolbox_pid() != toolbox_pid || owned.pid > 1) return;
    policy.attempted();
  }
  const uint64_t token = monotonic_token();
  if (!token) { set_state("error"); return; }
  char args[80]{};
  (void)std::snprintf(args, sizeof(args), "--hyperhen-r34 %llu --hyper-model %d",
                      static_cast<unsigned long long>(token), model_id);
  if (!audio_gate(false, model_id)) return;
  onion_notify(true, "notify.hyper.starting");
  LOG_INFO("Hyper R45: launching assistant model=%d through retained R33 loader", model_id);
  const pid_t pid = launch_worker(
      hyper_assistant_elf_start, hyper_assistant_elf_size, args, token);
  LOG_INFO("Hyper R45: assistant launch result pid=%d model=%d", static_cast<int>(pid), model_id);
  if (pid <= 1) {
    audio_gate(true);
    set_state("error");
    onion_notify(true, "notify.hyper.start_failed");
    return;
  }
  ProcessIdentity process;
  process.pid = pid;
  process.token = token;
  process.model_id = model_id;
  process.have_start = read_process_start(pid, &process.started);
  {
    std::lock_guard<std::mutex> guard(mutex);
    owned = process;
  }
  const ReadyWaitResult result = wait_for_ready(
      [&]() { return wants_on() && !daemon_power_state_is_sleeping() &&
                       selection().model_id == model_id && toolbox_is_current(toolbox_pid); },
      []() { return wire::install_in_progress(); },
      [&]() {
        wire::WorkerMarker marker;
        if (!process.have_start && read_process_start(pid, &process.started)) {
          process.have_start = true;
          std::lock_guard<std::mutex> guard(mutex);
          owned = process;
        }
        return same_process(process) && read_marker(&marker) &&
               wire::worker_marker_matches(marker, pid, token);
      },
      [&]() { return onion_proc_is_alive(pid); },
      []() { usleep(kPollUs); }, kReadyPolls);
  const bool installation_requested = result == ReadyWaitResult::Installing ||
                                      wire::install_in_progress();
  if (installation_requested) {
    // Preserve the false -> true installation event even if the installer
    // completes before the next regular one-second asset sample.
    installation_started();
  }
  const bool model_changed = selection().model_id != model_id;
  if (result != ReadyWaitResult::Ready || !wants_on() || installation_requested || model_changed) {
    stop_owned();
    if (wants_on() && !installation_requested && !model_changed) {
      set_state("error");
      onion_notify(true, "notify.hyper.start_failed");
    }
    return;
  }
  set_state("ready");
  LOG_INFO("Hyper R45: ready pid=%d revision=45", static_cast<int>(pid));
  onion_notify(true, "notify.hyper.ready");
}

void *run(void *) {
  LOG_INFO("Hyper R45: assistant data service owned by daemon; retained R33 util");
  bool previous_sleep = false;
  bool missing_notified = false;
  bool present = false;
  bool installing = false;
  bool verification_requested = false;
  bool verification_failed = false;
  unsigned verification_attempts = 0;
  unsigned seen_verification_epoch = 0;
  unsigned verification_cooldown = 0;
  unsigned verification_status_polls = 0;
  pid_t verification_service_pid = -1;
  unsigned asset_ticks = 5;
  int last_asset_state = -1;
  bool recovery_error_reported = false;
  while (!quit.load(std::memory_order_acquire) &&
         !g_stack_shutting_down.load(std::memory_order_acquire)) {
    const bool sleeping = daemon_power_state_is_sleeping();
    const int model_id = selection().model_id;
    const unsigned epoch = verification_epoch.load(std::memory_order_acquire);
    if (epoch != seen_verification_epoch) {
      seen_verification_epoch = epoch;
      verification_requested = verification_failed = false;
      verification_attempts = verification_cooldown = 0;
      verification_status_polls = 0;
      verification_service_pid = -1;
      missing_notified = false;
      asset_ticks = 5;
    }
    if (sleeping && !previous_sleep) toolbox_lost();
    previous_sleep = sleeping;
    if (++asset_ticks >= 5) {
#if defined(HYPERHEN_INTEGRATED_HYPER)
      if (wants_on() && !sleeping && current_owned().pid <= 1 &&
          wire::install_in_progress()) {
        const auto recovery = wire::recover_interrupted_install();
        if (recovery == wire::InstallRecoveryResult::Recovered) {
          LOG_INFO("Hyper R45: recovered interrupted installation marker; data unchanged");
          verification_requested = verification_failed = false;
          verification_attempts = verification_cooldown = 0;
          verification_status_polls = 0;
          recovery_error_reported = false;
        } else if ((recovery == wire::InstallRecoveryResult::Unsafe ||
                    recovery == wire::InstallRecoveryResult::Io) &&
                   !recovery_error_reported) {
          LOG_WARN("Hyper R45: installation recovery refused result=%d", static_cast<int>(recovery));
          recovery_error_reported = true;
        }
      }
#endif
      const bool ready = wire::assets_ready_for_model(model_id);
      present = ready || wire::assets_present_for_model(model_id);
      installing = wire::install_in_progress();
      const int asset_state = (ready ? 1 : 0) | (present ? 2 : 0) |
                              (installing ? 4 : 0) | (model_id << 3);
      if (asset_state != last_asset_state) {
        LOG_INFO("Hyper R45: assets model=%d present=%d ready=%d installing=%d", model_id,
                 present ? 1 : 0, ready ? 1 : 0, installing ? 1 : 0);
        last_asset_state = asset_state;
      }
      if (verification_cooldown) --verification_cooldown;
      std::lock_guard<std::mutex> guard(mutex);
      cached_assets = ready;
      policy.assets_changed(ready);
      asset_ticks = 0;
    }
    if (!wants_on() || sleeping) {
      audio_gate(true);
      stop_owned();
      set_state(wants_on() ? "waiting_toolbox" : "off");
      missing_notified = false;
      usleep(kPollUs);
      continue;
    }
    const ProcessIdentity process = current_owned();
    if (process.pid > 1) {
      const bool installation_requested = wire::install_in_progress();
      bool assets = false;
      int toolbox_pid = -1;
      {
        std::lock_guard<std::mutex> guard(mutex);
        assets = cached_assets;
        toolbox_pid = policy.toolbox_pid();
        if (installation_requested) {
          cached_assets = false;
          policy.assets_changed(false);
        }
      }
      if (process.model_id != selection().model_id || installation_requested ||
          !assets || !toolbox_is_current(toolbox_pid)) {
        stop_owned();
        usleep(kPollUs);
        continue;
      }
      if (!same_process(process)) {
        clear_our_marker(process);
        audio_gate(true);
        std::lock_guard<std::mutex> guard(mutex);
        owned = {};
        state = "error";
      }
      usleep(kPollUs);
      continue;
    }
    int toolbox_pid = -1;
    bool should_start = false;
    bool assets = false;
    {
      std::lock_guard<std::mutex> guard(mutex);
      assets = cached_assets;
      toolbox_pid = policy.toolbox_pid();
      should_start = policy.can_start(sleeping, false);
      if (!assets) state = "missing";
      else if (toolbox_pid <= 1) state = "waiting_toolbox";
    }
    // Existing R33 files need one full local hash verification before the
    // durable ready marker exists. This command never downloads anything.
    if (!assets && present && !installing && asset_ticks == 0 &&
        toolbox_is_current(toolbox_pid) && !verification_cooldown) {
      const pid_t service_pid = getpid();
      if (verification_requested && verification_service_pid > 1 &&
          service_pid > 1 && service_pid != verification_service_pid) {
        // An accepted job belonged to the old Hyper service. Its replacement
        // may verify again, within the same three-attempt budget.
        verification_requested = verification_failed = false;
      }
      if (!verification_requested && verification_attempts < 3 &&
          service_pid > 1 && wants_on()) {
        ++verification_attempts;
#if defined(HYPERHEN_INTEGRATED_HYPER)
        verification_requested = wire::start_verify_existing(model_id);
#else
        verification_requested = false;
#endif
        if (verification_requested) {
          verification_service_pid = service_pid;
          verification_status_polls = 0;
        }
        verification_cooldown = verification_requested ? 0 : 5;
        LOG_INFO("Hyper R45: existing assets local verification request accepted=%d attempt=%u",
                 verification_requested ? 1 : 0, verification_attempts);
      } else if (verification_requested && !verification_failed) {
        if (++verification_status_polls >= 600) verification_failed = true;
        if (!verification_failed) {
#if defined(HYPERHEN_INTEGRATED_HYPER)
          const std::string response = wire::install_status_json();
#else
          const std::string response;
#endif
          onion_cjson::Root parsed(response);
          const char *job_state = parsed ? onion_cjson::string_item(parsed.get(), "state") : nullptr;
          verification_failed = job_state &&
              (std::strcmp(job_state, "error") == 0 || std::strcmp(job_state, "cancelled") == 0);
        }
      }
    }
    if (!assets && present && !verification_failed) {
      set_state(toolbox_pid <= 1 ? "waiting_toolbox" :
          verification_attempts >= 3 && !verification_requested ? "error" : "starting");
    }
    if (!assets && (!present || verification_failed) && !installing && !missing_notified) {
      missing_notified = true;
      onion_notify(true, "notify.hyper.assets_missing");
    }
    if (assets) missing_notified = false;
    if (should_start) start_worker(toolbox_pid, model_id);
    usleep(kPollUs);
  }
  stop_owned();
  set_state("off");
  running.store(false, std::memory_order_release);
  return nullptr;
}
} // namespace

onion::hyper::RuntimeSelection selection() {
  return selected.get();
}

bool set_selection(int model_id, int voice_id) {
  return selected.apply({model_id, voice_id},
      [](int id) { return wire::model_ready_fast(id); },
      [](int id) { return wire::voice_entry(id) != nullptr; },
      [](const wire::RuntimeSelection &value) { return wire::write_runtime_selection(value); },
      []() {
        audio_gate(true);
        verification_epoch.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> guard(mutex);
        cached_assets = false;
        policy.assets_changed(false);
      });
}

void set_enabled(bool value) {
  const bool previous = enabled.exchange(value, std::memory_order_acq_rel);
  if (value && !previous) verification_epoch.fetch_add(1, std::memory_order_acq_rel);
  if (!value) audio_gate(true);
  std::lock_guard<std::mutex> guard(mutex);
  policy.set_enabled(value);
  if (!value && owned.pid <= 1) state = "off";
}

void toolbox_ready(pid_t pid) {
  std::lock_guard<std::mutex> guard(mutex);
  policy.toolbox_ready(static_cast<int>(pid));
}

void toolbox_lost() {
  std::lock_guard<std::mutex> guard(mutex);
  policy.toolbox_lost();
}

bool start() {
  bool expected = false;
  if (!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return true;
  quit.store(false, std::memory_order_release);
  audio_gate(true);
  if (pthread_create(&thread, nullptr, run, nullptr) == 0) return true;
  running.store(false, std::memory_order_release);
  set_state("error");
  return false;
}

void stop() {
  enabled.store(false, std::memory_order_release);
  quit.store(true, std::memory_order_release);
  audio_gate(true);
  // The owner thread also observes cancellation during its READY wait.
  if (thread) { (void)pthread_join(thread, nullptr); thread = nullptr; }
  stop_owned();
  set_state("off");
}

std::string status_json() {
  std::lock_guard<std::mutex> guard(mutex);
  char json[384]{};
  const auto selected = selection();
  (void)std::snprintf(json, sizeof(json),
      "{\"enabled\":%s,\"state\":\"%s\",\"pid\":%d,\"assets_ready\":%s,\"model_id\":%d,\"voice_id\":%d,\"running_model_id\":%d}",
      enabled.load(std::memory_order_acquire) ? "true" : "false", state,
      static_cast<int>(owned.pid), cached_assets ? "true" : "false", selected.model_id,
      selected.voice_id, owned.pid > 1 ? owned.model_id : -1);
  return json;
}
} // namespace onion::daemon::hyper
