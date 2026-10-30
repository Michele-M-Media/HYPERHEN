#include "hyper_tts.hpp"
#include <onion/hyper_voice_catalog.hpp>

#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>

#include <cerrno>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <new>
#include <sys/stat.h>

namespace {
constexpr size_t kMaximumCharacters = 500;
constexpr size_t kMaximumBytes = kMaximumCharacters * 4;
constexpr size_t kMaximumSeconds = 40;
std::atomic_flag engine_busy = ATOMIC_FLAG_INIT;
bool engine_unusable = false;

struct EngineLock {
  bool owned = !engine_busy.test_and_set(std::memory_order_acquire);
  ~EngineLock() { if (owned) engine_busy.clear(std::memory_order_release); }
};

struct Synthesis {
  HyperTtsAudio *audio;
  bool (*cancel)(void *);
  void *user_data;
  size_t maximum_samples;
  int failure = 0;
};
// Accessed only synchronously, while EngineLock is held.
Synthesis *current = nullptr;

bool cancelled(const Synthesis &synthesis) {
  return synthesis.cancel && synthesis.cancel(synthesis.user_data);
}

int validate_text(const std::string &text) {
  if (text.empty()) return -EINVAL;
  if (text.size() > kMaximumBytes) return -E2BIG;
  size_t characters = 0;
  bool nonspace = false;
  for (size_t offset = 0; offset < text.size();) {
    const auto first = static_cast<unsigned char>(text[offset]);
    uint32_t value = 0, minimum = 0;
    size_t width = 0;
    if (first < 0x80) { value = first; width = 1; }
    else if (first >= 0xc2 && first <= 0xdf) {
      value = first & 0x1fu; width = 2; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      value = first & 0x0fu; width = 3; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      value = first & 0x07u; width = 4; minimum = 0x10000;
    } else return -EILSEQ;
    if (width > text.size() - offset) return -EILSEQ;
    for (size_t index = 1; index < width; ++index) {
      const auto next = static_cast<unsigned char>(text[offset + index]);
      if ((next & 0xc0u) != 0x80u) return -EILSEQ;
      value = (value << 6) | (next & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) return -EILSEQ;
    if (value == 0 || (value < 0x20u && value != '\n' && value != '\r' && value != '\t'))
      return -EINVAL;
    if (value != ' ' && value != '\n' && value != '\r' && value != '\t')
      nonspace = true;
    if (++characters > kMaximumCharacters) return -E2BIG;
    offset += width;
  }
  return nonspace ? 0 : -EINVAL;
}

int validate_data(const char *path) {
  if (!path || !*path || std::strlen(path) > 240) return -EINVAL;
  struct stat info{};
  if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) return -ENOENT;
  // Require this exact directory; InitializePath otherwise falls back to HOME
  // or a system installation, masking a missing package in host tests.
  constexpr const char *required[] = {
      "phondata", "phonindex", "phontab", "intonations"};
  for (const char *name : required) {
    const std::string file = std::string(path) + "/" + name;
    if (stat(file.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
      return -ENOENT;
  }
  return 0;
}

int canonical_voice(const char *voice, char (&canonical)[40]) {
  if (!voice || !*voice || std::strlen(voice) >= sizeof(canonical)) return -EINVAL;
  size_t offset = 0;
  for (; voice[offset]; ++offset) {
    char value = voice[offset];
    if (value >= 'A' && value <= 'Z') value = static_cast<char>(value + ('a' - 'A'));
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-'))
      return -EINVAL;
    canonical[offset] = value;
  }
  canonical[offset] = '\0';
  return 0;
}

bool declares_language(const espeak_VOICE *voice, const char *language) {
  if (!voice || !voice->languages) return false;
  // eSpeak stores [priority byte][NUL-terminated tag] entries, then NUL.
  const char *tag = voice->languages;
  while (*tag) {
    ++tag;
    if (std::strcmp(tag, language) == 0) return true;
    tag += std::strlen(tag) + 1;
  }
  return false;
}

int voice_identifier(const char *language, char (&identifier)[256]) {
  const espeak_VOICE **voices = espeak_ListVoices(nullptr);
  if (!voices) return -ENOENT;
  for (size_t index = 0; voices[index]; ++index) {
    if (!declares_language(voices[index], language) || !voices[index]->identifier)
      continue;
    const size_t bytes = std::strlen(voices[index]->identifier) + 1;
    if (bytes > sizeof(identifier)) return -ENAMETOOLONG;
    std::memcpy(identifier, voices[index]->identifier, bytes);
    return 0;
  }
  return -ENOENT;
}

int receive_pcm(short *samples, int count, espeak_EVENT *events) {
  Synthesis *synthesis = current;
  if (!synthesis) return 1;
  if (cancelled(*synthesis)) { synthesis->failure = -ECANCELED; return 1; }
  if (events) {
    for (const espeak_EVENT *event = events;
         event->type != espeakEVENT_LIST_TERMINATED; ++event) {
      if (event->type == espeakEVENT_SAMPLERATE &&
          event->id.number != synthesis->audio->sample_rate) {
        synthesis->failure = -EPROTO; return 1;
      }
    }
  }
  if (!samples) return count == 0 ? 0 : (synthesis->failure = -EIO, 1);
  if (count < 0 || static_cast<size_t>(count) >
      synthesis->maximum_samples - synthesis->audio->samples.size()) {
    synthesis->failure = -EOVERFLOW; return 1;
  }
  static_assert(sizeof(short) == sizeof(int16_t));
  try {
    synthesis->audio->samples.insert(synthesis->audio->samples.end(),
                                    samples, samples + count);
  } catch (const std::bad_alloc &) {
    synthesis->failure = -ENOMEM; return 1;
  }
  return 0;
}
} // namespace

void hyper_tts_clear(HyperTtsAudio &audio) noexcept {
  auto *bytes = reinterpret_cast<volatile unsigned char *>(audio.samples.data());
  size_t remaining = audio.samples.size() * sizeof(int16_t);
  while (remaining--) *bytes++ = 0;
  std::vector<int16_t>().swap(audio.samples);
  audio.sample_rate = 0;
}

int hyper_tts(const std::string &text, HyperTtsAudio &audio, const char *voice,
              bool (*cancel)(void *), void *user_data, const char *data_path,
              int voice_style) {
  hyper_tts_clear(audio);
  int result = validate_text(text);
  if (result != 0) return result;
  char selected_voice[40]{};
  if ((result = canonical_voice(voice, selected_voice)) != 0) return result;
  const auto *style = onion::hyper::voice_entry(voice_style);
  if (!style) return -EINVAL;
  if (cancel && cancel(user_data)) return -ECANCELED;
  EngineLock lock;
  if (!lock.owned) return -EBUSY;
  if (engine_unusable) return -EIO;
  try { result = validate_data(data_path); }
  catch (const std::bad_alloc &) { return -ENOMEM; }
  if (result != 0) return result;

  espeak_ng_InitializePath(data_path);
  espeak_ng_ERROR_CONTEXT error_context = nullptr;
  const espeak_ng_STATUS initialized = espeak_ng_Initialize(&error_context);
  espeak_ng_ClearErrorContext(&error_context);
  if (initialized != ENS_OK) {
    if (espeak_ng_Terminate() != ENS_OK) engine_unusable = true;
    return -EIO;
  }
  auto status = espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 60, nullptr);
  char selected_identifier[256]{};
  if (status == ENS_OK) result = voice_identifier(selected_voice, selected_identifier);
  if (status == ENS_OK && result == 0 && style->variant[0]) {
    const size_t used = std::strlen(selected_identifier);
    const size_t suffix = std::strlen(style->variant);
    if (used + 1 + suffix >= sizeof(selected_identifier)) result = -ENAMETOOLONG;
    else {
      selected_identifier[used] = '+';
      std::memcpy(selected_identifier + used + 1, style->variant, suffix + 1);
    }
  }
  // SetVoiceByName accepts a voice-file identifier, not every advertised
  // language tag: fr-fr is advertised by roa/fr, for example.
  if (status == ENS_OK && result == 0)
    status = espeak_ng_SetVoiceByName(selected_identifier);
  if (status == ENS_OK && result == 0) {
    // The current voice can collapse e.g. fr-fr to translator language fr.
    // Match its actual voice-file identifier to the advertised exact tag.
    const espeak_VOICE *selected = espeak_GetCurrentVoice();
    if (!selected || !selected->identifier ||
        std::strcmp(selected->identifier, selected_identifier) != 0) result = -ENOENT;
  }
  audio.sample_rate = espeak_ng_GetSampleRate();
  if (status != ENS_OK || audio.sample_rate < 8000 || audio.sample_rate > 48000)
    result = status == ENS_VOICE_NOT_FOUND ? -ENOENT : -EIO;
  Synthesis synthesis{&audio, cancel, user_data,
                      static_cast<size_t>(audio.sample_rate > 0 ? audio.sample_rate : 0) *
                          kMaximumSeconds};
  if (result == 0) {
    try { audio.samples.reserve(synthesis.maximum_samples); }
    catch (const std::bad_alloc &) { result = -ENOMEM; }
  }
  if (result == 0) {
    current = &synthesis;
    espeak_SetSynthCallback(receive_pcm);
    status = espeak_ng_Synthesize(text.c_str(), text.size() + 1, 0,
                                 POS_CHARACTER, 0, espeakCHARS_UTF8, nullptr, &synthesis);
    current = nullptr;
    espeak_SetSynthCallback(nullptr);
    if (synthesis.failure != 0) result = synthesis.failure;
    else if (status != ENS_OK) result = -EIO;
    else if (cancelled(synthesis)) result = -ECANCELED;
    else if (audio.samples.empty()) result = -ENOMSG;
  }
  if (espeak_ng_Terminate() != ENS_OK) {
    engine_unusable = true; result = -EIO;
  }
  if (result != 0) hyper_tts_clear(audio);
  return result;
}

int hyper_tts_italian(const std::string &text, HyperTtsAudio &audio,
                     bool (*cancel)(void *), void *user_data,
                     const char *data_path) {
  return hyper_tts(text, audio, "it", cancel, user_data, data_path);
}
