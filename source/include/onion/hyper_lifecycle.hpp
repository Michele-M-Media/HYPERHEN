#pragma once

#include <cstdint>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace onion::hyper {
inline constexpr char kWorkerMarker[] = "/system_tmp/hyper_assistant_r34.pid";
inline constexpr char kDisabledMarker[] = "/system_tmp/hyper_assistant_r34.disabled";
inline constexpr char kWorkerLock[] = "/system_tmp/hyper_audio_probe_p1.lock";
inline constexpr char kWorkerStage[] = "/system_tmp/hyper_assistant_r34.elf";

struct WorkerMarker {
  int pid = -1;
  uint64_t token = 0;
};

inline bool parse_worker_marker(const char *text, WorkerMarker *out) {
  if (!text || !out) return false;
  WorkerMarker value;
  constexpr char prefix[] = "hyper-r34 34 ";
  if (std::strncmp(text, prefix, sizeof(prefix) - 1) != 0) return false;
  const char *cursor = text + sizeof(prefix) - 1;
  if (*cursor < '1' || *cursor > '9') return false;
  char *end = nullptr;
  errno = 0;
  const long pid = std::strtol(cursor, &end, 10);
  if (errno || pid <= 1 || pid > INT_MAX || !end || *end != ' ') return false;
  cursor = end + 1;
  if (*cursor < '1' || *cursor > '9') return false;
  errno = 0;
  const unsigned long long token = std::strtoull(cursor, &end, 10);
  if (errno || !token || !end || *end != '\0') return false;
  value.pid = static_cast<int>(pid);
  value.token = static_cast<uint64_t>(token);
  *out = value;
  return true;
}

inline bool worker_marker_matches(const WorkerMarker &marker, int pid,
                                   uint64_t token) {
  return pid > 1 && token != 0 && marker.pid == pid && marker.token == token;
}

// One attempt per useful event. A failed or exited worker is never restarted
// merely because another timer tick elapsed.
class LaunchPolicy {
 public:
  void set_enabled(bool value) {
    if (enabled_ != value) { enabled_ = value; ++epoch_; }
  }
  void toolbox_ready(int pid) {
    if (pid > 1 && toolbox_pid_ != pid) { toolbox_pid_ = pid; ++epoch_; }
  }
  void toolbox_lost() { toolbox_pid_ = -1; }
  void assets_changed(bool ready) {
    if (ready && !assets_ready_) ++epoch_;
    assets_ready_ = ready;
  }
  bool can_start(bool sleeping, bool shutting_down) const {
    return enabled_ && assets_ready_ && toolbox_pid_ > 1 && !sleeping &&
           !shutting_down && attempted_epoch_ != epoch_;
  }
  void attempted() { attempted_epoch_ = epoch_; }
  int toolbox_pid() const { return toolbox_pid_; }
 private:
  bool enabled_ = true;
  bool assets_ready_ = false;
  int toolbox_pid_ = -1;
  uint64_t epoch_ = 1;
  uint64_t attempted_epoch_ = 0;
};
} // namespace onion::hyper
