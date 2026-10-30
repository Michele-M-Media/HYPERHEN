#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct whisper_context;

inline constexpr const char *kHyperSttModelPath =
    "/data/Hyper/models/ggml-tiny.bin";
inline constexpr std::size_t kHyperSttRate = 16000;
inline constexpr std::size_t kHyperSttMaxSamples = 12 * kHyperSttRate;
inline constexpr std::size_t kHyperSttMaxTextBytes = 512;

enum class HyperSttStatus {
    Ok,
    NotLoaded,
    InvalidInput,
    InvalidLanguage,
    InvalidModelPath,
    ModelUnavailable,
    UnsupportedModel,
    ModelLoadFailed,
    NoMemory,
    Silence,
    NoSpeech,
    Cancelled,
    TimedOut,
    InferenceFailed,
    OutputLimit,
    ClockFailed,
};

const char *hyper_stt_status_name(HyperSttStatus status) noexcept;

struct HyperSttResult {
    HyperSttStatus status = HyperSttStatus::NotLoaded;
    std::string text;
    std::uint64_t elapsed_ms = 0;
};

// Worker-owned and serialized: never call the same instance concurrently, and
// never instantiate it in ShellUI. All audio is already mono float PCM at 16kHz.
// The callback must remain valid for the duration of the synchronous operation.
// true means cancel. No transcript or PCM is logged or written to disk here.
class HyperStt {
public:
    HyperStt() = default;
    ~HyperStt();
    HyperStt(const HyperStt &) = delete;
    HyperStt &operator=(const HyperStt &) = delete;
    HyperStt(HyperStt &&) = delete;
    HyperStt &operator=(HyperStt &&) = delete;

    HyperSttStatus load(const std::string &model_path = kHyperSttModelPath,
                        bool (*cancel)(void *) = nullptr,
                        void *user = nullptr);
    void unload() noexcept;
    bool loaded() const noexcept { return context_ != nullptr; }

    // At most 12 seconds, at least 0.5 seconds, finite values in [-1,1].
    // Decoder is CPU only, two threads, greedy, without previous text
    // context, translation, timestamps or fixed textual prompts. A soft 30s
    // inference limit uses Whisper's cancellation callback; the worker's own
    // watchdog remains necessary if a native call does not return. language is
    // a Whisper-supported code such as "it", "en", "ja". nullptr, "" or "auto"
    // selects genuine model language detection; it never translates the text.
    HyperSttResult transcribe(const std::vector<float> &pcm16k,
                              bool (*cancel)(void *) = nullptr,
                              void *user = nullptr,
                              const char *language = "auto");

private:
    whisper_context *context_ = nullptr;
};
