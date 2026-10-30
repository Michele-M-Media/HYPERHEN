#ifndef HYPER_P14_VOICE_WIRE_H
#define HYPER_P14_VOICE_WIRE_H
#include <stdint.h>

#define HYPER_P14_VOICE_SOCKET "/system_tmp/hyper_p14_voice.sock"
#define HYPER_P14_VOICE_MAGIC UINT32_C(0x34564148)
#define HYPER_P14_VOICE_VERSION UINT32_C(1)
#define HYPER_P14_HELLO UINT32_C(1)
#define HYPER_P14_CAPTURE UINT32_C(2)
#define HYPER_P14_REPLY UINT32_C(3)
#define HYPER_P14_DONE UINT32_C(4)
#define HYPER_P14_MAX_INPUT_SAMPLES UINT32_C(192000)
#define HYPER_P14_MAX_OUTPUT_SAMPLES UINT32_C(1920000)
#define HYPER_P14_AUDIO_REQUEST UINT32_C(8)

// AF_UNIX stream only. Samples follow the capture/reply header as signed PCM16.
// The initial HELLO is accompanied by kernel SCM_CREDS for sender identity.
struct hyper_p14_voice_header {
    uint32_t magic, version, kind, sample_rate, samples;
    int32_t status, user;
    uint32_t reserved;
    uint64_t sequence;
};
#ifdef __cplusplus
static_assert(sizeof(hyper_p14_voice_header) == 40);
#else
_Static_assert(sizeof(struct hyper_p14_voice_header) == 40, "voice frame size");
#endif
#endif
