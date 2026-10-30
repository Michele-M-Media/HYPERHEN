#pragma once

#include <cstdint>

constexpr uint64_t kHyperVoiceInputFreshMs = 350;
constexpr uint64_t kHyperVoiceProcessingFreshMs = 5000;

enum class HyperVoiceGuardReason : unsigned {
  active = 0,
  disabled = 1,
  foreground = 2,
  provider = 3,
  clock_or_lock = 4,
  no_sample = 5,
  circle = 6,
  intercepted = 7,
  generation = 8,
  stale = 9
};

struct HyperVoiceSessionInput {
  uint64_t now, observed_ms, sequence;
  uint32_t buttons;
  uint64_t cancellation, consent_generation, start_generation;
  bool latched;
};

inline HyperVoiceGuardReason hyper_voice_session_reason(
    const HyperVoiceSessionInput &input, uint64_t max_age_ms) {
  if (input.now == UINT64_MAX || input.now < input.observed_ms)
    return HyperVoiceGuardReason::clock_or_lock;
  if (!input.sequence) return HyperVoiceGuardReason::no_sample;
  if (input.buttons & 0x2000u) return HyperVoiceGuardReason::circle;
  if (input.buttons & 0x80000000u) return HyperVoiceGuardReason::intercepted;
  if (input.cancellation != input.consent_generation ||
      (input.latched && input.cancellation != input.start_generation))
    return HyperVoiceGuardReason::generation;
  if (input.now - input.observed_ms >= max_age_ms)
    return HyperVoiceGuardReason::stale;
  return HyperVoiceGuardReason::active;
}

inline bool hyper_voice_terminal_notice_allowed(bool enabled, bool disabled_marker,
                                               int user, int foreground) {
  return enabled && !disabled_marker && user > 0 && user != 0xfe &&
      user != 0xff && user == foreground;
}
