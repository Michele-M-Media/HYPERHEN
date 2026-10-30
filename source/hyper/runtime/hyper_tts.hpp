#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HyperTtsAudio {
  std::vector<int16_t> samples;
  int sample_rate = 0;
};

// Synchronous TTS, in memory only. A true cancellation callback aborts.
// Accepts canonical UTF-8, at most 500 codepoints, and at most 40 s of mono PCM.
// Returns zero or a negative errno-style error; clears partial audio on error.
// eSpeak has process-global state: concurrent calls return -EBUSY.
// The requested voice must be an exact declared language tag in installed data;
// unavailable regional variants fail instead of silently selecting another voice.
int hyper_tts(const std::string &text, HyperTtsAudio &audio, const char *voice,
              bool (*cancel)(void *) = nullptr, void *user_data = nullptr,
              const char *data_path = "/data/Hyper/espeak-ng-data",
              int voice_style = 0);

// Compatibility wrapper for the original Italian-only prototype.
int hyper_tts_italian(const std::string &text, HyperTtsAudio &audio,
                     bool (*cancel)(void *) = nullptr, void *user_data = nullptr,
                     const char *data_path = "/data/Hyper/espeak-ng-data");

// Clear caller-owned PCM before releasing its allocation.
void hyper_tts_clear(HyperTtsAudio &audio) noexcept;
