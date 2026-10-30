#pragma once

#include <cstddef>
#include <cstdint>

// Called after capture has closed the microphone. Buffers remain caller-owned.
// Returns zero on success or a negative errno/worker status; errors leave count zero.
int hyper_p14_voice_exchange(int user, std::uint64_t sequence, bool (*active)(int),
                             const std::int16_t *input, std::size_t input_samples,
                             std::int16_t *output, std::size_t output_capacity,
                             std::size_t *output_samples);

// On success transfer the authenticated stream to the caller until playback cleanup.
int hyper_p14_voice_exchange_session(int user, std::uint64_t sequence, bool (*active)(int),
                                     const std::int16_t *input, std::size_t input_samples,
                                     std::int16_t *output, std::size_t output_capacity,
                                     std::size_t *output_samples, int *session_fd);

// Send playback/cleanup completion and always close the transferred stream.
int hyper_p14_voice_finish(int *session_fd, int user, std::uint64_t sequence,
                           bool (*active)(int), int status, std::size_t samples);
