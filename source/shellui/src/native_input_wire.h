#ifndef HYPER_P5_NATIVE_INPUT_WIRE_H
#define HYPER_P5_NATIVE_INPUT_WIRE_H

#include <stdint.h>

#define HYPER_P5_INPUT_SOCKET "/system_tmp/hyper_p5_input.sock"
#define HYPER_P5_AUDIO_SOCKET "/system_tmp/hyper_p5_audio.sock"
#define HYPER_P5_INPUT_MAGIC UINT32_C(0x35504848)
#define HYPER_P5_INPUT_VERSION UINT32_C(1)
#define HYPER_P5_AUDIO_MAGIC UINT32_C(0x35414848)
#define HYPER_P5_AUDIO_VERSION UINT32_C(1)
#define HYPER_P5_AUDIO_PLAY_STARTUP UINT32_C(1)
#define HYPER_P5_AUDIO_PROBE_INPUT UINT32_C(2)
#define HYPER_P5_AUDIO_MONITOR_MUTE UINT32_C(3)
#define HYPER_P5_AUDIO_CAPTURE_ECHO UINT32_C(4)
#define HYPER_P5_AUDIO_PROBE_ROUTE UINT32_C(5)
#define HYPER_P5_AUDIO_PLAY_TONES UINT32_C(6)
#define HYPER_P13_AUDIO_CAPTURE_ECHO UINT32_C(7)

/* Local datagram: no audio payload, handle value or memory address. */
struct hyper_p5_input_frame {
    uint32_t magic;
    uint32_t version;
    uint64_t sequence;
    uint32_t buttons;
    int32_t device_index;
};

/* One-way diagnostic request. It never carries samples or audio handles. */
struct hyper_p5_audio_command {
    uint32_t magic;
    uint32_t version;
    uint32_t action;
    int32_t user;
    uint64_t sequence;
};

#if defined(__cplusplus)
static_assert(sizeof(hyper_p5_input_frame) == 24);
static_assert(sizeof(hyper_p5_audio_command) == 24);
#else
_Static_assert(sizeof(struct hyper_p5_input_frame) == 24, "input frame size");
_Static_assert(sizeof(struct hyper_p5_audio_command) == 24, "audio command size");
#endif

#endif
