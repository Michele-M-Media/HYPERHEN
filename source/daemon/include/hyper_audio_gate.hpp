#pragma once

#include <mutex>

namespace onion::daemon::hyper {
// This lock covers only the final enable decision and marker mutation. It is
// never held during loader IPC, READY waits, process exit, or status reads.
class AudioGate {
 public:
  template <typename Allowed, typename Disable, typename Enable>
  bool apply(bool disabled, Allowed allowed, Disable disable, Enable enable) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (disabled) return disable();
    return allowed() && enable();
  }
 private:
  std::mutex mutex_;
};
} // namespace onion::daemon::hyper
