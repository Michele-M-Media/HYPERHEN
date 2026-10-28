#pragma once

#include <atomic>
#include <cstdint>

/* Lock-free publication per GetData call. The independent cancellation epoch
 * preserve cancellation even if a newer release frame replaces its sample.
 * A packed tag+buttons value prevents torn observations with multiple callers. */
class HyperInputMailbox {
 public:
  static constexpr uint32_t Cancel = 0x80002000u;
  struct State {
    uint32_t tag;
    uint32_t buttons;
    uint64_t cancellation;
    uint64_t transitions;
  };

  void publish(uint32_t buttons) noexcept {
    const uint32_t tag = static_cast<uint32_t>(
        next_.fetch_add(1, std::memory_order_relaxed) + 1);
    if (buttons & Cancel)
      cancellation_.fetch_add(1, std::memory_order_release);
    const uint32_t encoded = encode(buttons);
    uint64_t previous = packed_.load(std::memory_order_relaxed);
    while (static_cast<int32_t>(tag - static_cast<uint32_t>(previous >> 32)) > 0) {
      const uint32_t transitions = ((static_cast<uint32_t>(previous) >> 6) +
          ((static_cast<uint32_t>(previous) & 63u) != encoded ? 1u : 0u)) & TransitionMask;
      const uint64_t candidate = (static_cast<uint64_t>(tag) << 32) |
          (static_cast<uint64_t>(transitions) << 6) | encoded;
      if (packed_.compare_exchange_strong(previous, candidate,
              std::memory_order_release, std::memory_order_relaxed))
        return;
    }
    /* An older concurrent caller may publish after a newer snapshot. Its
     * differing state still interrupts the gesture even though its snapshot
     * must not overwrite the newer tag. Identical late callers are harmless. */
    if ((static_cast<uint32_t>(previous) & 63u) != encoded)
      cancellation_.fetch_add(1, std::memory_order_release);
  }

  State state() const noexcept {
    const uint64_t packed = packed_.load(std::memory_order_acquire);
    return {static_cast<uint32_t>(packed >> 32), decode(static_cast<uint32_t>(packed) & 63u),
            cancellation(), (static_cast<uint32_t>(packed) >> 6) & TransitionMask};
  }
  uint64_t cancellation() const noexcept {
    return cancellation_.load(std::memory_order_acquire);
  }
  void cancel() noexcept { cancellation_.fetch_add(1, std::memory_order_release); }
  static constexpr uint32_t TransitionMask = 0x03ffffffu;

 private:
  static uint32_t encode(uint32_t buttons) noexcept {
    return (buttons & 1u) | ((buttons & 0x400u) ? 2u : 0u) |
        ((buttons & 0x800u) ? 4u : 0u) | ((buttons & 0x8000u) ? 8u : 0u) |
        ((buttons & 0x2000u) ? 16u : 0u) | ((buttons & 0x80000000u) ? 32u : 0u);
  }
  static uint32_t decode(uint32_t buttons) noexcept {
    return (buttons & 1u) | ((buttons & 2u) ? 0x400u : 0u) |
        ((buttons & 4u) ? 0x800u : 0u) | ((buttons & 8u) ? 0x8000u : 0u) |
        ((buttons & 16u) ? 0x2000u : 0u) | ((buttons & 32u) ? 0x80000000u : 0u);
  }
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  std::atomic<uint64_t> next_{0};
  std::atomic<uint64_t> packed_{48u};
  std::atomic<uint64_t> cancellation_{0};
};

class HyperInputCursor {
 public:
  struct Sample { uint64_t sequence; uint32_t buttons; bool gap; };

  void discard(const HyperInputMailbox &mailbox) noexcept {
    const auto state = mailbox.state();
    tag_ = state.tag;
    buttons_ = state.buttons;
    cancellation_ = state.cancellation;
    transitions_ = state.transitions;
  }

  void acknowledge_cancellation(const HyperInputMailbox &mailbox) noexcept {
    cancellation_ = mailbox.cancellation();
  }

  bool take(const HyperInputMailbox &mailbox, Sample &sample) noexcept {
    const auto state = mailbox.state();
    const uint32_t delta = state.tag - tag_;
    const bool advanced = static_cast<int32_t>(delta) > 0;
    const bool cancelled = state.cancellation != cancellation_;
    const uint64_t changes = (state.transitions - transitions_) & HyperInputMailbox::TransitionMask;
    if (!advanced && !cancelled && changes == 0) return false;
    const uint64_t expected_changes = state.buttons == buttons_ ? 0 : 1;
    /* A cancellation-only update still needs a new wire sequence so the
     * standalone worker cannot keep using its previous held-button sample. */
    sequence_ += advanced ? delta : 1;
    sample = {sequence_, cancelled ? state.buttons | HyperInputMailbox::Cancel
                                  : state.buttons,
              cancelled || !advanced || changes != expected_changes};
    if (advanced) tag_ = state.tag;
    buttons_ = state.buttons;
    cancellation_ = state.cancellation;
    transitions_ = state.transitions;
    return true;
  }

 private:
  uint32_t tag_ = 0;
  uint32_t buttons_ = HyperInputMailbox::Cancel;
  uint64_t cancellation_ = 0;
  uint64_t transitions_ = 0;
  uint64_t sequence_ = 0;
};
