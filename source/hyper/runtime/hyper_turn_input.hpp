#pragma once
#include <cstdint>
#include <cerrno>
extern "C" {
#include "activation_gate.h"
}

enum class HyperTurnInputReason : unsigned {
    none = 0, unavailable = 1, stale = 2, clock = 3, user_changed = 4,
    disconnected = 5, circle = 6, intercepted = 7, read_error = 8,
    generation = 9, stopping = 10, deadline = 11, mutex = 12
};

// Serialized by the input mutex. The longer interval is limited to computation
// after capture has closed; starting a turn and transferring PCM remain strict.
struct HyperTurnInputState {
    static constexpr std::uint64_t strict_ms = 350;
    static constexpr std::uint64_t processing_ms = 5000;
    hyper_p5_sample sample{};
    std::uint64_t received_ms = 0, generation = 0;
    HyperTurnInputReason last_reason = HyperTurnInputReason::none;
    bool valid = false, processing = false;

    std::uint64_t limit() const noexcept { return processing ? processing_ms : strict_ms; }
    std::uint64_t age(std::uint64_t now) const noexcept {
        return valid && now != UINT64_MAX && now >= received_ms ? now-received_ms : UINT64_MAX;
    }
    bool fresh(std::uint64_t now, std::uint64_t maximum = strict_ms) const noexcept {
        return valid && sample.connected && age(now) < maximum;
    }
    void invalidate(HyperTurnInputReason why) noexcept { ++generation; last_reason = why; }
    void observe(const hyper_p5_sample &next, std::uint64_t now, int code) noexcept {
        if (now == UINT64_MAX || (code < 0 && code != -EAGAIN && code != -EWOULDBLOCK)) {
            if (valid) invalidate(now == UINT64_MAX ? HyperTurnInputReason::clock : HyperTurnInputReason::read_error);
            valid = false;
            return;
        }
        if (code < 0 || !next.sequence || (valid && next.sequence <= sample.sequence)) return;
        HyperTurnInputReason why = HyperTurnInputReason::none;
        if (!next.connected) why = HyperTurnInputReason::disconnected;
        else if (next.buttons & HYPER_P5_CIRCLE) why = HyperTurnInputReason::circle;
        else if (next.buttons & HYPER_P5_INTERCEPTED) why = HyperTurnInputReason::intercepted;
        else if (valid && next.user != sample.user) why = HyperTurnInputReason::user_changed;
        else if (valid && now < received_ms) why = HyperTurnInputReason::clock;
        else if (valid && now-received_ms >= limit()) why = HyperTurnInputReason::stale;
        if (why != HyperTurnInputReason::none) invalidate(why);
        sample = next; received_ms = now; valid = true;
    }
    HyperTurnInputReason evaluate(int user, std::uint64_t expected, std::uint64_t now) const noexcept {
        if (generation != expected)
            return last_reason == HyperTurnInputReason::none ? HyperTurnInputReason::generation : last_reason;
        if (!valid) return HyperTurnInputReason::unavailable;
        if (sample.user != user) return HyperTurnInputReason::user_changed;
        if (!sample.connected) return HyperTurnInputReason::disconnected;
        if (sample.buttons & HYPER_P5_CIRCLE) return HyperTurnInputReason::circle;
        if (sample.buttons & HYPER_P5_INTERCEPTED) return HyperTurnInputReason::intercepted;
        if (now == UINT64_MAX || now < received_ms) return HyperTurnInputReason::clock;
        return age(now) >= limit() ? HyperTurnInputReason::stale : HyperTurnInputReason::none;
    }
};
