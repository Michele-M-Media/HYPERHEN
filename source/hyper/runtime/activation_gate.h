#ifndef HYPER_P5_ACTIVATION_GATE_H
#define HYPER_P5_ACTIVATION_GATE_H

#include <stdbool.h>
#include <stdint.h>

#define HYPER_P5_L1 UINT32_C(0x400)
#define HYPER_P5_R1 UINT32_C(0x800)
#define HYPER_P5_SQUARE UINT32_C(0x8000)
#define HYPER_P5_CIRCLE UINT32_C(0x2000)
#define HYPER_P5_INTERCEPTED UINT32_C(0x80000000)
#define HYPER_P5_COMBO (HYPER_P5_L1 | HYPER_P5_R1 | HYPER_P5_SQUARE)
#define HYPER_P5_HOLD_MS UINT64_C(600)
#define HYPER_P5_STALE_MS UINT64_C(300)
#define HYPER_P5_WAIT_MS UINT64_C(60000)

/* This is a transport-independent gate, NOT a controller access mechanism.
 * Its caller must supply samples from a verified, same-user input provider.
 * The timestamp is reception time on the worker's monotonic clock, not an
 * unchecked timestamp supplied over IPC. Sequence numbers count fresh reads. */
struct hyper_p5_sample {
    uint64_t sequence;
    int user;
    uint32_t buttons;
    bool connected;
};

enum hyper_p5_event {
    HYPER_P5_WAIT,
    HYPER_P5_START,
    HYPER_P5_CANCEL,
    HYPER_P5_EXPIRED
};

struct hyper_p5_gate {
    uint64_t start_ms, last_ms, sample_ms, hold_ms, sequence;
    int user;
    bool have_sample, released, holding, active, finished;
};

bool hyper_p5_user_valid(int user);
bool hyper_p5_gate_init(struct hyper_p5_gate *gate, int user, uint64_t now);
enum hyper_p5_event hyper_p5_gate_step(struct hyper_p5_gate *gate,
                                      uint64_t now,
                                      const struct hyper_p5_sample *sample);
void hyper_p5_gate_finish(struct hyper_p5_gate *gate);

#endif
