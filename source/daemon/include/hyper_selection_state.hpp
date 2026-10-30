#pragma once
#include <onion/hyper_runtime_selection.hpp>
#include <atomic>
#include <mutex>

namespace onion::daemon::hyper {
class SelectionState {
 public:
  onion::hyper::RuntimeSelection get() const {
    const unsigned value = selected_.load(std::memory_order_acquire);
    return {static_cast<int>(value & 255u), static_cast<int>((value >> 8u) & 255u)};
  }
  template <typename ModelReady, typename VoiceValid, typename Publish, typename ModelChanged>
  bool apply(const onion::hyper::RuntimeSelection &requested, ModelReady model_ready,
             VoiceValid voice_valid, Publish publish, ModelChanged model_changed) {
    if (!onion::hyper::valid_runtime_selection(requested) || !voice_valid(requested.voice_id))
      return false;
    std::lock_guard<std::mutex> guard(mutex_);
    const auto previous = get();
    const bool changed_model = previous.model_id != requested.model_id;
    if (changed_model && !model_ready(requested.model_id)) return false;
    if (!publish(requested)) return false;
    selected_.store(static_cast<unsigned>(requested.model_id) |
        (static_cast<unsigned>(requested.voice_id) << 8u), std::memory_order_release);
    if (changed_model) model_changed();
    return true;
  }
 private:
  mutable std::mutex mutex_;
  std::atomic<unsigned> selected_{0};
};
} // namespace onion::daemon::hyper
