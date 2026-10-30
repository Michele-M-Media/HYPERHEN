#include "activation_gate.h"
#include <string.h>

bool hyper_p5_user_valid(int user) {
    return user > 0 && user != 0xfe && user != 0xff;
}

bool hyper_p5_gate_init(struct hyper_p5_gate *gate, int user, uint64_t now) {
    if (!gate) return false;
    memset(gate, 0, sizeof(*gate));
    gate->user = user;
    gate->start_ms = gate->last_ms = gate->sample_ms = now;
    gate->finished = !hyper_p5_user_valid(user) || now == UINT64_MAX;
    return !gate->finished;
}

static enum hyper_p5_event stop(struct hyper_p5_gate *gate,
                                enum hyper_p5_event event) {
    gate->finished = true;
    gate->holding = false;
    gate->released = false;
    return event;
}

enum hyper_p5_event hyper_p5_gate_step(struct hyper_p5_gate *gate,
                                      uint64_t now,
                                      const struct hyper_p5_sample *sample) {
    if (!gate || gate->finished) return HYPER_P5_EXPIRED;
    if (now == UINT64_MAX || now < gate->last_ms)
        return stop(gate, HYPER_P5_CANCEL);
    gate->last_ms = now;
    if (!gate->active && now - gate->start_ms >= HYPER_P5_WAIT_MS)
        return stop(gate, HYPER_P5_EXPIRED);

    /* A replayed, missing or stalled feed cannot sustain a held combo. */
    if (!sample || (gate->have_sample && sample->sequence == gate->sequence)) {
        if (now - gate->sample_ms >= HYPER_P5_STALE_MS) {
            gate->released = gate->holding = false;
            if (gate->active) return stop(gate, HYPER_P5_CANCEL);
        }
        return HYPER_P5_WAIT;
    }
    if (gate->have_sample && sample->sequence < gate->sequence)
        return stop(gate, HYPER_P5_CANCEL);
    /* A newly arriving sample does not retroactively fill a stale interval. */
    if (gate->have_sample && now - gate->sample_ms >= HYPER_P5_STALE_MS) {
        gate->released = gate->holding = false;
        if (gate->active) return stop(gate, HYPER_P5_CANCEL);
    }
    gate->have_sample = true;
    gate->sample_ms = now;
    gate->sequence = sample->sequence;
    if (sample->user != gate->user || !sample->connected)
        return stop(gate, HYPER_P5_CANCEL);

    /* The payload menu is normally closed with Circle after this worker is
     * launched. Before activation that is navigation, not voice cancellation.
     * Still allow Circle to cancel immediately once the mic session is live. */
    if (sample->buttons & HYPER_P5_CIRCLE) {
        gate->released = gate->holding = false;
        return gate->active ? stop(gate, HYPER_P5_CANCEL) : HYPER_P5_WAIT;
    }

    if (sample->buttons & HYPER_P5_INTERCEPTED) {
        gate->released = gate->holding = false;
        return gate->active ? stop(gate, HYPER_P5_CANCEL) : HYPER_P5_WAIT;
    }
    if (gate->active) return HYPER_P5_WAIT;
    if (sample->buttons == 0) gate->released = true;
    /* Other held buttons do not accidentally trigger the voice action. */
    if (!gate->released || sample->buttons != HYPER_P5_COMBO) {
        gate->holding = false;
        return HYPER_P5_WAIT;
    }
    if (!gate->holding) {
        gate->holding = true;
        gate->hold_ms = now;
    } else if (now - gate->hold_ms >= HYPER_P5_HOLD_MS) {
        gate->active = true;
        gate->holding = false;
        return HYPER_P5_START;
    }
    return HYPER_P5_WAIT;
}

void hyper_p5_gate_finish(struct hyper_p5_gate *gate) {
    if (gate) (void)stop(gate, HYPER_P5_EXPIRED);
}
