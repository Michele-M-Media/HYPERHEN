#pragma once

#include "hyper_tts.hpp"
extern "C" {
#include "hyper_voice_wire.h"
}
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Canonical UTF-8 prefix, limited on a word boundary when a reply is too long.
// If no word boundary exists, CJK/Thai/non-ASCII text uses a complete-codepoint
// prefix. An overlong ASCII-only word or malformed UTF-8 returns an empty string.
std::string hyper_worker_limit_reply(const std::string &text,
                                    std::size_t maximum_bytes = 480);

// Duration-preserving mono interpolation. No file or audio-device operations.
// At most 40 seconds of 48kHz PCM; partial output is erased on failure.
int hyper_worker_resample_48k(const HyperTtsAudio &source,
                              std::vector<std::int16_t> &output,
                              bool (*cancel)(void *) = nullptr,
                              void *user = nullptr);

// DONE is accepted only on the already authenticated voice connection. Zero
// acknowledges exact playback + drain + cleanup; a negative peer status with
// zero samples reports failure. Positive statuses or mismatched identity fail.
int hyper_worker_validate_done(const hyper_p14_voice_header &frame, int user,
                                std::uint64_t sequence,
                                std::uint32_t expected_samples) noexcept;

void hyper_worker_erase(std::vector<std::int16_t> &pcm) noexcept;
void hyper_worker_erase(std::vector<float> &pcm) noexcept;
void hyper_worker_erase(std::string &text) noexcept;
