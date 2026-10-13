/* Copyright (C) 2026 HyperHEN contributors */

#include "bootstrap_audio.h"

#include <onion/log.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const unsigned char hyperhen_hype_voice_pcm_start[];
extern const unsigned char hyperhen_hype_voice_pcm_end[];

extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int user_id, int port_type, int port_index,
                           uint32_t grain, uint32_t sample_rate,
                           uint32_t format);
extern int sceAudioOutSetVolume(int handle, int flags,
                                const int32_t *volume);
extern int sceAudioOutOutput(int handle, const void *buffer);
extern int sceAudioOutClose(int handle);
extern int sceUserServiceInitialize(const void *params);
extern int sceUserServiceGetForegroundUser(int *user_id);
extern int sceUserServiceGetInitialUser(int *user_id);
extern int sceUserServiceTerminate(void);

enum {
  kAudioSystemUser = 0xff,
  kAudioMainPort = 0,
  kAudioPortIndex = 0,
  kAudioGrainFrames = 1024,
  kAudioSampleRate = 48000,
  kAudioFormatS16Stereo = 1,
  kAudioVolumeStereo = 3,
  kAudioStartupVolume = 0x7fff,
  kAudioChannels = 2,
  kAudioBytesPerSample = 2,
  kAudioChunkBytes =
      kAudioGrainFrames * kAudioChannels * kAudioBytesPerSample,
};

static bool append_user_candidate(int users[3], size_t *count, int user_id) {
  if (user_id < 0 || !count || *count >= 3)
    return false;
  for (size_t i = 0; i < *count; ++i) {
    if (users[i] == user_id)
      return false;
  }
  users[(*count)++] = user_id;
  return true;
}

static int open_audio_for_active_user(bool *terminate_user_service) {
  int users[3] = {-1, -1, -1};
  size_t user_count = 0;
  int user_id = -1;

  const int init_result = sceUserServiceInitialize(NULL);
  *terminate_user_service = init_result >= 0;
  if (init_result < 0) {
    LOG_WARN("Startup voice: sceUserServiceInitialize returned 0x%08x; "
             "probing existing service",
             (unsigned int)init_result);
  }

  if (sceUserServiceGetForegroundUser(&user_id) >= 0)
    (void)append_user_candidate(users, &user_count, user_id);
  user_id = -1;
  if (sceUserServiceGetInitialUser(&user_id) >= 0)
    (void)append_user_candidate(users, &user_count, user_id);
  (void)append_user_candidate(users, &user_count, kAudioSystemUser);

  for (size_t i = 0; i < user_count; ++i) {
    const int handle = sceAudioOutOpen(
        users[i], kAudioMainPort, kAudioPortIndex, kAudioGrainFrames,
        kAudioSampleRate, kAudioFormatS16Stereo);
    if (handle >= 0) {
      LOG_INFO("Startup voice: audio opened for user %d", users[i]);
      return handle;
    }
    LOG_WARN("Startup voice: sceAudioOutOpen user=%d failed (0x%08x)",
             users[i], (unsigned int)handle);
  }
  return -1;
}

static void terminate_user_service_if_owned(bool owned) {
  if (owned)
    (void)sceUserServiceTerminate();
}

static bool audio_call_succeeded(int result, const char *operation) {
  if (result >= 0)
    return true;
  LOG_WARN("Startup voice: %s failed (0x%08x)", operation,
           (unsigned int)result);
  return false;
}

bool bootstrap_audio_play_hype_voice(void) {
  const size_t pcm_size = (size_t)(hyperhen_hype_voice_pcm_end -
                                   hyperhen_hype_voice_pcm_start);
  if (pcm_size == 0) {
    LOG_WARN("Startup voice: embedded PCM is empty");
    return false;
  }

  const int audio_init_result = sceAudioOutInit();
  if (audio_init_result < 0) {
    LOG_WARN("Startup voice: sceAudioOutInit returned 0x%08x; attempting "
             "to use an existing audio service",
             (unsigned int)audio_init_result);
  }

  bool terminate_user_service = false;
  const int handle = open_audio_for_active_user(&terminate_user_service);
  if (handle < 0) {
    terminate_user_service_if_owned(terminate_user_service);
    return false;
  }

  const int32_t volume[kAudioChannels] = {kAudioStartupVolume,
                                          kAudioStartupVolume};
  (void)audio_call_succeeded(
      sceAudioOutSetVolume(handle, kAudioVolumeStereo, volume),
      "sceAudioOutSetVolume");

  const unsigned char *cursor = hyperhen_hype_voice_pcm_start;
  size_t remaining = pcm_size;
  while (remaining >= kAudioChunkBytes) {
    if (!audio_call_succeeded(sceAudioOutOutput(handle, cursor),
                              "sceAudioOutOutput")) {
      (void)sceAudioOutClose(handle);
      terminate_user_service_if_owned(terminate_user_service);
      return false;
    }
    cursor += kAudioChunkBytes;
    remaining -= kAudioChunkBytes;
  }

  if (remaining != 0) {
    int16_t final_chunk[kAudioGrainFrames * kAudioChannels]
        __attribute__((aligned(64))) = {0};
    memcpy(final_chunk, cursor, remaining);
    if (!audio_call_succeeded(sceAudioOutOutput(handle, final_chunk),
                              "sceAudioOutOutput(final)")) {
      (void)sceAudioOutClose(handle);
      terminate_user_service_if_owned(terminate_user_service);
      return false;
    }
  }

  /* A null submission drains the final queued grain before closing the port. */
  const bool drained = audio_call_succeeded(
      sceAudioOutOutput(handle, NULL), "sceAudioOutOutput(drain)");
  const bool closed = audio_call_succeeded(sceAudioOutClose(handle),
                                           "sceAudioOutClose");
  terminate_user_service_if_owned(terminate_user_service);
  return drained && closed;
}
