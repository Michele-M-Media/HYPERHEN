#pragma once

#include <cstdint>

namespace onion {

enum class ToolboxWebkitDecision {
  Waiting,
  Ready,
  TargetChanged,
  QueryFailed,
  TimedOut,
  InvalidClock,
};

/* Pure timing policy; successful process enumeration is an explicit input.
 * Never turn an unavailable snapshot into evidence that WebKit has exited. */
class ToolboxWebkitGate final {
public:
  ToolboxWebkitGate(int target, uint64_t started_ms,
                   uint64_t timeout_ms = 600000, uint64_t quiet_ms = 2000)
      : target_(target), started_ms_(started_ms), timeout_ms_(timeout_ms),
        quiet_ms_(quiet_ms), previous_ms_(started_ms) {}

  ToolboxWebkitDecision observe(uint64_t now_ms, bool query_ok,
                                int shellui_pid, int web_pid) {
    if (now_ms < previous_ms_ || now_ms < started_ms_)
      return ToolboxWebkitDecision::InvalidClock;
    previous_ms_ = now_ms;
    if (!query_ok)
      return ToolboxWebkitDecision::QueryFailed;
    if (target_ <= 1 || shellui_pid != target_)
      return ToolboxWebkitDecision::TargetChanged;
    if (now_ms - started_ms_ >= timeout_ms_)
      return ToolboxWebkitDecision::TimedOut;
    if (web_pid > 1) {
      quiet_started_ = false;
      return ToolboxWebkitDecision::Waiting;
    }
    if (!quiet_started_) {
      quiet_started_ = true;
      quiet_since_ms_ = now_ms;
    }
    return now_ms - quiet_since_ms_ >= quiet_ms_
               ? ToolboxWebkitDecision::Ready
               : ToolboxWebkitDecision::Waiting;
  }

private:
  int target_;
  uint64_t started_ms_;
  uint64_t timeout_ms_;
  uint64_t quiet_ms_;
  uint64_t previous_ms_;
  uint64_t quiet_since_ms_ = 0;
  bool quiet_started_ = false;
};

} // namespace onion
