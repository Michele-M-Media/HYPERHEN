/* Copyright (C) 2026 HyperHEN contributors */

#include "startup_audio.h"

#include <onion/log.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const unsigned char hyperhen_shellui_hype_voice_pcm_start[];
extern const unsigned char hyperhen_shellui_hype_voice_pcm_end[];

extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int user_id, int port_type, int port_index,
                           uint32_t grain, uint32_t sample_rate,
                           uint32_t format);
extern int sceAudioOutSetVolume(int handle, int flags,
                                const int32_t *volume);
extern int sceAudioOutOutput(int handle, const void *buffer);
extern int sceAudioOutClose(int handle);
extern int sceUserServiceGetForegroundUser(int *user_id);
extern int sceUserServiceGetInitialUser(int *user_id);

enum {
  kAudioSystemUser = 0xff,
  kAudioMainPort = 0,
  kAudioBgmPort = 1,
  kAudioPadSpeakerPort = 4,
  kAudioPortIndex = 0,
  kAudioGrainFrames = 1024,
  kAudioSampleRate = 48000,
  kAudioFormatS16Mono = 0,
  kAudioFormatS16Stereo = 1,
  kAudioVolumeMono = 1,
  kAudioVolumeStereo = 3,
  kAudioStartupVolume = 0x8000,
  kAudioVolumeChannels = 8,
  kAudioChannels = 2,
  kAudioBytesPerSample = 2,
  kAudioChunkBytes =
      kAudioGrainFrames * kAudioChannels * kAudioBytesPerSample,
};

typedef struct ShellUiAudioRoute {
  int handle;
  int channels;
  const char *name;
} ShellUiAudioRoute;

static bool append_user_candidate(int users[2], size_t *count, int user_id) {
  if (user_id < 0 || !count || *count >= 2)
    return false;
  for (size_t i = 0; i < *count; ++i) {
    if (users[i] == user_id)
      return false;
  }
  users[(*count)++] = user_id;
  return true;
}

static ShellUiAudioRoute open_shellui_audio(void) {
  int users[2] = {-1, -1};
  size_t user_count = 0;
  int user_id = -1;
  ShellUiAudioRoute route = {-1, 0, "none"};

  if (sceUserServiceGetForegroundUser(&user_id) >= 0)
    (void)append_user_candidate(users, &user_count, user_id);
  user_id = -1;
  if (sceUserServiceGetInitialUser(&user_id) >= 0)
    (void)append_user_candidate(users, &user_count, user_id);

  for (size_t i = 0; i < user_count; ++i) {
    const int handle = sceAudioOutOpen(
        users[i], kAudioPadSpeakerPort, kAudioPortIndex, kAudioGrainFrames,
        kAudioSampleRate, kAudioFormatS16Mono);
    if (handle >= 0) {
      route.handle = handle;
      route.channels = 1;
      route.name = "DualSense speaker";
      LOG_INFO("Startup voice: SceShellUI opened DualSense for user %d",
               users[i]);
      return route;
    }
    LOG_WARN("Startup voice: SceShellUI DualSense user=%d failed "
             "(0x%08x)",
             users[i], (unsigned int)handle);
  }

  for (size_t i = 0; i < user_count; ++i) {
    const int handle = sceAudioOutOpen(
        users[i], kAudioMainPort, kAudioPortIndex, kAudioGrainFrames,
        kAudioSampleRate, kAudioFormatS16Stereo);
    if (handle >= 0) {
      route.handle = handle;
      route.channels = kAudioChannels;
      route.name = "SceShellUI main output";
      LOG_INFO("Startup voice: SceShellUI opened main output for user %d",
               users[i]);
      return route;
    }
    LOG_WARN("Startup voice: SceShellUI main user=%d failed (0x%08x)",
             users[i], (unsigned int)handle);
  }

  int handle = sceAudioOutOpen(
      kAudioSystemUser, kAudioMainPort, kAudioPortIndex, kAudioGrainFrames,
      kAudioSampleRate, kAudioFormatS16Stereo);
  if (handle >= 0) {
    route.handle = handle;
    route.channels = kAudioChannels;
    route.name = "SceShellUI system output";
    LOG_INFO("Startup voice: SceShellUI opened system main output");
    return route;
  }
  LOG_WARN("Startup voice: SceShellUI system main failed (0x%08x)",
           (unsigned int)handle);

  for (size_t i = 0; i < user_count; ++i) {
    handle = sceAudioOutOpen(users[i], kAudioBgmPort, kAudioPortIndex,
                             kAudioGrainFrames, kAudioSampleRate,
                             kAudioFormatS16Stereo);
    if (handle >= 0) {
      route.handle = handle;
      route.channels = kAudioChannels;
      route.name = "SceShellUI BGM output";
      LOG_INFO("Startup voice: SceShellUI opened BGM output for user %d",
               users[i]);
      return route;
    }
    LOG_WARN("Startup voice: SceShellUI BGM user=%d failed (0x%08x)",
             users[i], (unsigned int)handle);
  }

  return route;
}

static bool audio_call_succeeded(int result, const char *operation) {
  if (result >= 0)
    return true;
  LOG_WARN("Startup voice: SceShellUI %s failed (0x%08x)", operation,
           (unsigned int)result);
  return false;
}

static bool output_stereo_pcm(int handle, const unsigned char *pcm,
                              size_t pcm_size) {
  const unsigned char *cursor = pcm;
  size_t remaining = pcm_size;
  while (remaining >= kAudioChunkBytes) {
    if (!audio_call_succeeded(sceAudioOutOutput(handle, cursor),
                              "sceAudioOutOutput"))
      return false;
    cursor += kAudioChunkBytes;
    remaining -= kAudioChunkBytes;
  }

  if (remaining != 0) {
    int16_t final_chunk[kAudioGrainFrames * kAudioChannels]
        __attribute__((aligned(256))) = {0};
    memcpy(final_chunk, cursor, remaining);
    if (!audio_call_succeeded(sceAudioOutOutput(handle, final_chunk),
                              "sceAudioOutOutput(final)"))
      return false;
  }
  return true;
}

static bool output_mono_pcm(int handle, const unsigned char *pcm,
                            size_t pcm_size) {
  const int16_t *source = (const int16_t *)pcm;
  size_t frames_remaining =
      pcm_size / (kAudioChannels * kAudioBytesPerSample);

  while (frames_remaining != 0) {
    const size_t frames = frames_remaining < kAudioGrainFrames
                              ? frames_remaining
                              : kAudioGrainFrames;
    int16_t mono_chunk[kAudioGrainFrames] __attribute__((aligned(256))) = {0};
    for (size_t frame = 0; frame < frames; ++frame) {
      const int32_t mixed = (int32_t)source[frame * 2] +
                            (int32_t)source[frame * 2 + 1];
      mono_chunk[frame] = (int16_t)(mixed / 2);
    }
    if (!audio_call_succeeded(sceAudioOutOutput(handle, mono_chunk),
                              "sceAudioOutOutput(DualSense)"))
      return false;
    source += frames * kAudioChannels;
    frames_remaining -= frames;
  }
  return true;
}

bool shellui_play_startup_voice(void) {
  const size_t pcm_size =
      (size_t)(hyperhen_shellui_hype_voice_pcm_end -
               hyperhen_shellui_hype_voice_pcm_start);
  if (pcm_size == 0 ||
      (pcm_size % (kAudioChannels * kAudioBytesPerSample)) != 0) {
    LOG_WARN("Startup voice: SceShellUI embedded PCM is invalid");
    return false;
  }

  const int init_result = sceAudioOutInit();
  if (init_result < 0) {
    LOG_DEBUG("Startup voice: SceShellUI audio service already active "
              "(0x%08x)",
              (unsigned int)init_result);
  }

  const ShellUiAudioRoute route = open_shellui_audio();
  if (route.handle < 0)
    return false;

  int32_t volume[kAudioVolumeChannels];
  for (size_t channel = 0; channel < kAudioVolumeChannels; ++channel)
    volume[channel] = kAudioStartupVolume;
  (void)audio_call_succeeded(
      sceAudioOutSetVolume(route.handle,
                           route.channels == 1 ? kAudioVolumeMono
                                               : kAudioVolumeStereo,
                           volume),
      "sceAudioOutSetVolume");

  const bool output_ok =
      route.channels == 1
          ? output_mono_pcm(route.handle,
                            hyperhen_shellui_hype_voice_pcm_start, pcm_size)
          : output_stereo_pcm(route.handle,
                              hyperhen_shellui_hype_voice_pcm_start, pcm_size);
  if (!output_ok) {
    (void)sceAudioOutClose(route.handle);
    return false;
  }

  int16_t silence[kAudioGrainFrames * kAudioChannels]
      __attribute__((aligned(256))) = {0};
  if (!audio_call_succeeded(sceAudioOutOutput(route.handle, silence),
                            "sceAudioOutOutput(tail)")) {
    (void)sceAudioOutClose(route.handle);
    return false;
  }

  const bool drained = audio_call_succeeded(
      sceAudioOutOutput(route.handle, NULL), "sceAudioOutOutput(drain)");
  const bool closed = audio_call_succeeded(sceAudioOutClose(route.handle),
                                           "sceAudioOutClose");
  if (drained && closed)
    LOG_INFO("Startup voice: playback completed on %s", route.name);
  return drained && closed;
}
