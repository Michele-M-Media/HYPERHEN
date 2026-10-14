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
  kUserServicePriority = 256,
  kAudioChannels = 2,
  kAudioBytesPerSample = 2,
  kAudioChunkBytes =
      kAudioGrainFrames * kAudioChannels * kAudioBytesPerSample,
};

typedef struct BootstrapAudioRoute {
  int handle;
  int channels;
  const char *name;
} BootstrapAudioRoute;

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

static BootstrapAudioRoute
open_audio_for_active_user(bool *terminate_user_service) {
  int users[3] = {-1, -1, -1};
  static const int port_types[] = {kAudioMainPort, kAudioBgmPort};
  size_t user_count = 0;
  int user_id = -1;
  const int user_service_priority = kUserServicePriority;
  BootstrapAudioRoute route = {-1, 0, "none"};

  const int init_result = sceUserServiceInitialize(&user_service_priority);
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

  /* The main port can accept buffers without routing them from a short-lived
   * payload process.  Prefer the controller speaker, whose port is explicitly
   * associated with the logged-in user, so a successful call is audible. */
  for (size_t i = 0; i < user_count; ++i) {
    const int handle = sceAudioOutOpen(
        users[i], kAudioPadSpeakerPort, kAudioPortIndex, kAudioGrainFrames,
        kAudioSampleRate, kAudioFormatS16Mono);
    if (handle >= 0) {
      LOG_INFO("Startup voice: audio opened for user %d on DualSense speaker",
               users[i]);
      route.handle = handle;
      route.channels = 1;
      route.name = "DualSense speaker";
      return route;
    }
    LOG_WARN("Startup voice: DualSense speaker for user %d failed "
             "(0x%08x)",
             users[i], (unsigned int)handle);
  }

  /* Keep HDMI/TV and BGM routes as compatibility fallbacks for systems where
   * no controller speaker is available to the foreground user. */
  int handle = sceAudioOutOpen(
      kAudioSystemUser, kAudioMainPort, kAudioPortIndex, kAudioGrainFrames,
      kAudioSampleRate, kAudioFormatS16Stereo);
  if (handle >= 0) {
    LOG_INFO("Startup voice: audio opened for system user on main port");
    route.handle = handle;
    route.channels = kAudioChannels;
    route.name = "main output";
    return route;
  }
  LOG_WARN("Startup voice: system main port failed (0x%08x); trying "
           "per-user fallbacks",
           (unsigned int)handle);
  (void)append_user_candidate(users, &user_count, kAudioSystemUser);

  for (size_t port = 0; port < sizeof(port_types) / sizeof(port_types[0]);
       ++port) {
    for (size_t i = 0; i < user_count; ++i) {
      handle = sceAudioOutOpen(
          users[i], port_types[port], kAudioPortIndex, kAudioGrainFrames,
          kAudioSampleRate, kAudioFormatS16Stereo);
      if (handle >= 0) {
        LOG_INFO("Startup voice: audio opened for user %d on port %d",
                 users[i], port_types[port]);
        route.handle = handle;
        route.channels = kAudioChannels;
        route.name = port_types[port] == kAudioMainPort ? "main output"
                                                        : "BGM output";
        return route;
      }
      LOG_WARN(
          "Startup voice: sceAudioOutOpen user=%d port=%d failed (0x%08x)",
          users[i], port_types[port], (unsigned int)handle);
    }
  }
  return route;
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

bool bootstrap_audio_play_hype_voice(void) {
  const size_t pcm_size = (size_t)(hyperhen_hype_voice_pcm_end -
                                   hyperhen_hype_voice_pcm_start);
  if (pcm_size == 0) {
    LOG_WARN("Startup voice: embedded PCM is empty");
    return false;
  }
  if ((pcm_size % (kAudioChannels * kAudioBytesPerSample)) != 0) {
    LOG_WARN("Startup voice: embedded PCM does not contain complete frames");
    return false;
  }

  const int audio_init_result = sceAudioOutInit();
  if (audio_init_result < 0) {
    LOG_WARN("Startup voice: sceAudioOutInit returned 0x%08x; attempting "
             "to use an existing audio service",
             (unsigned int)audio_init_result);
  }

  bool terminate_user_service = false;
  const BootstrapAudioRoute route =
      open_audio_for_active_user(&terminate_user_service);
  if (route.handle < 0) {
    terminate_user_service_if_owned(terminate_user_service);
    return false;
  }

  int32_t volume[kAudioVolumeChannels];
  for (size_t channel = 0; channel < kAudioVolumeChannels; ++channel)
    volume[channel] = kAudioStartupVolume;
  (void)audio_call_succeeded(
      sceAudioOutSetVolume(route.handle,
                           route.channels == 1 ? kAudioVolumeMono
                                               : kAudioVolumeStereo,
                           volume),
      "sceAudioOutSetVolume");

  const bool output_ok = route.channels == 1
                             ? output_mono_pcm(route.handle,
                                               hyperhen_hype_voice_pcm_start,
                                               pcm_size)
                             : output_stereo_pcm(route.handle,
                                                 hyperhen_hype_voice_pcm_start,
                                                 pcm_size);
  if (!output_ok) {
    (void)sceAudioOutClose(route.handle);
    terminate_user_service_if_owned(terminate_user_service);
    return false;
  }

  /*
   * Queue one silent grain so closing the short-lived bootstrapper port cannot
   * clip the final syllable.  The null submission then waits for that final
   * queued grain to leave the port.
   */
  int16_t silence[kAudioGrainFrames * kAudioChannels]
      __attribute__((aligned(256))) = {0};
  if (!audio_call_succeeded(sceAudioOutOutput(route.handle, silence),
                            "sceAudioOutOutput(tail)")) {
    (void)sceAudioOutClose(route.handle);
    terminate_user_service_if_owned(terminate_user_service);
    return false;
  }

  const bool drained = audio_call_succeeded(
      sceAudioOutOutput(route.handle, NULL), "sceAudioOutOutput(drain)");
  const bool closed = audio_call_succeeded(sceAudioOutClose(route.handle),
                                           "sceAudioOutClose");
  if (drained && closed)
    LOG_INFO("Startup voice: playback completed on %s", route.name);
  terminate_user_service_if_owned(terminate_user_service);
  return drained && closed;
}
