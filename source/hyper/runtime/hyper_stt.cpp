#include "hyper_stt.hpp"

#include <whisper.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr std::uint64_t kInferenceLimitMs = 30000;
constexpr std::size_t kMaximumModelBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMinimumModelBytes = 1024u * 1024u;
constexpr std::size_t kMinimumSamples = kHyperSttRate / 2;

bool cancelled(bool (*callback)(void *), void *user) noexcept {
    if (!callback) return false;
    try { return callback(user); }
    catch (...) { return true; }
}

bool now_ms(std::uint64_t &value) noexcept {
    timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0 || time.tv_sec < 0)
        return false;
    value = static_cast<std::uint64_t>(time.tv_sec) * 1000u +
            static_cast<std::uint64_t>(time.tv_nsec) / 1000000u;
    return true;
}

// Whisper has one process-global logger. This adapter runs in an isolated
// worker which owns all Whisper contexts, so disable that logger permanently.
// Parameters below also disable every transcript-printing decoder option.
void discard_log(ggml_log_level, const char *, void *) {}

struct FileLoader {
    int fd = -1;
    bool ended = false;
    bool failed = false;
    ~FileLoader() { close_file(); }

    void close_file() noexcept {
        if (fd >= 0) { (void)::close(fd); fd = -1; }
    }
    static std::size_t read(void *user, void *output, std::size_t size) {
        auto &self = *static_cast<FileLoader *>(user);
        auto *bytes = static_cast<unsigned char *>(output);
        std::size_t completed = 0;
        while (completed < size) {
            const ssize_t count = ::read(self.fd, bytes + completed,
                                         size - completed);
            if (count > 0) { completed += static_cast<std::size_t>(count); continue; }
            if (count < 0 && errno == EINTR) continue;
            self.ended = true;
            self.failed = count < 0;
            // Upstream read_safe currently ignores returned byte counts.
            // Do not let a short read leave uninitialized scalar data behind.
            std::memset(bytes + completed, 0, size - completed);
            break;
        }
        return completed;
    }
    static bool eof(void *user) { return static_cast<FileLoader *>(user)->ended; }
    static void close(void *user) { static_cast<FileLoader *>(user)->close_file(); }
};

std::uint32_t little_u32(const unsigned char *bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           static_cast<std::uint32_t>(bytes[1]) << 8 |
           static_cast<std::uint32_t>(bytes[2]) << 16 |
           static_cast<std::uint32_t>(bytes[3]) << 24;
}

bool allowed_path(const std::string &path) {
    if (path.find('\0') != std::string::npos) return false;
#ifdef HYPER_STT_HOST_TEST_PATHS
    // Host-only smoke harness permits an explicit absolute fixture path. This
    // macro must never be set in the console runtime target.
    return !path.empty() && path.front() == '/' && path.size() < 4096 &&
           path.find("/../") == std::string::npos;
#else
    return path == kHyperSttModelPath;
#endif
}

HyperSttStatus open_tiny(const std::string &path, FileLoader &loader) {
    if (!allowed_path(path)) return HyperSttStatus::InvalidModelPath;
    loader.fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (loader.fd < 0) return HyperSttStatus::ModelUnavailable;
    struct stat info{};
    if (::fstat(loader.fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < static_cast<off_t>(kMinimumModelBytes) ||
        info.st_size > static_cast<off_t>(kMaximumModelBytes))
        return HyperSttStatus::UnsupportedModel;

    // Exact scalar order of whisper_model_load in pinned revision
    // 284029c07012d2000941a08d38a31ffb71f4734e. Limit allocation to the
    // intended multilingual tiny F16 architecture before entering the loader.
    // Integrity/authenticity is additionally checked by the model installer.
    unsigned char header[48]{};
    if (FileLoader::read(&loader, header, sizeof(header)) != sizeof(header))
        return HyperSttStatus::UnsupportedModel;
    constexpr std::uint32_t expected[12] = {
        0x67676d6cu, 51865u, 1500u, 384u, 6u, 4u,
        448u, 384u, 6u, 4u, 80u, 1u};
    for (std::size_t index = 0; index < 12; ++index)
        if (little_u32(header + index * 4) != expected[index])
            return HyperSttStatus::UnsupportedModel;
    if (::lseek(loader.fd, 0, SEEK_SET) != 0) return HyperSttStatus::ModelUnavailable;
    loader.ended = loader.failed = false;
    return HyperSttStatus::Ok;
}

struct AbortState {
    bool (*cancel)(void *);
    void *user;
    std::uint64_t started;
    HyperSttStatus reason = HyperSttStatus::Ok;

    bool check() noexcept {
        if (reason != HyperSttStatus::Ok) return true;
        if (cancelled(cancel, user)) { reason = HyperSttStatus::Cancelled; return true; }
        std::uint64_t now = 0;
        if (!now_ms(now) || now < started) { reason = HyperSttStatus::ClockFailed; return true; }
        if (now - started >= kInferenceLimitMs) { reason = HyperSttStatus::TimedOut; return true; }
        return false;
    }
    static bool abort(void *user) { return static_cast<AbortState *>(user)->check(); }
    static bool begin(whisper_context *, whisper_state *, void *user) {
        return !static_cast<AbortState *>(user)->check();
    }
};

bool utf8_valid(const std::string &text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80) continue;
        unsigned remaining = 0;
        std::uint32_t value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { remaining = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { remaining = 2; value = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (remaining > text.size() - index) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

void trim_and_clean(std::string &text) {
    std::size_t output = 0;
    bool space = true;
    for (unsigned char value : text) {
        if (value <= 0x20 || value == 0x7f) {
            if (!space && output) text[output++] = ' ';
            space = true;
        } else {
            text[output++] = static_cast<char>(value); space = false;
        }
    }
    if (output && text[output - 1] == ' ') --output;
    text.resize(output);
}
} // namespace

const char *hyper_stt_status_name(HyperSttStatus status) noexcept {
    switch (status) {
    case HyperSttStatus::Ok: return "ok";
    case HyperSttStatus::NotLoaded: return "not_loaded";
    case HyperSttStatus::InvalidInput: return "invalid_input";
    case HyperSttStatus::InvalidLanguage: return "invalid_language";
    case HyperSttStatus::InvalidModelPath: return "invalid_model_path";
    case HyperSttStatus::ModelUnavailable: return "model_unavailable";
    case HyperSttStatus::UnsupportedModel: return "unsupported_model";
    case HyperSttStatus::ModelLoadFailed: return "model_load_failed";
    case HyperSttStatus::NoMemory: return "no_memory";
    case HyperSttStatus::Silence: return "silence";
    case HyperSttStatus::NoSpeech: return "no_speech";
    case HyperSttStatus::Cancelled: return "cancelled";
    case HyperSttStatus::TimedOut: return "timed_out";
    case HyperSttStatus::InferenceFailed: return "inference_failed";
    case HyperSttStatus::OutputLimit: return "output_limit";
    case HyperSttStatus::ClockFailed: return "clock_failed";
    }
    return "unknown";
}

HyperStt::~HyperStt() { unload(); }

void HyperStt::unload() noexcept {
    if (context_) { whisper_free(context_); context_ = nullptr; }
}

HyperSttStatus HyperStt::load(const std::string &path,
                              bool (*cancel)(void *), void *user) {
    unload();
    if (cancelled(cancel, user)) return HyperSttStatus::Cancelled;
    FileLoader file;
    const auto opened = open_tiny(path, file);
    if (opened != HyperSttStatus::Ok) return opened;
    whisper_log_set(discard_log, nullptr);
    auto parameters = whisper_context_default_params();
    parameters.use_gpu = false;
    parameters.flash_attn = false;
    parameters.dtw_token_timestamps = false;
    whisper_model_loader loader{&file, FileLoader::read, FileLoader::eof, FileLoader::close};
    try { context_ = whisper_init_with_params(&loader, parameters); }
    catch (const std::bad_alloc &) { return HyperSttStatus::NoMemory; }
    catch (...) { return HyperSttStatus::ModelLoadFailed; }
    if (!context_) return HyperSttStatus::ModelLoadFailed;
    if (file.failed || whisper_is_multilingual(context_) != 1 ||
        whisper_model_n_audio_layer(context_) != 4 ||
        whisper_model_n_text_layer(context_) != 4 ||
        whisper_model_n_audio_state(context_) != 384) {
        unload(); return HyperSttStatus::UnsupportedModel;
    }
    if (cancelled(cancel, user)) { unload(); return HyperSttStatus::Cancelled; }
    return HyperSttStatus::Ok;
}

HyperSttResult HyperStt::transcribe(const std::vector<float> &samples,
                                    bool (*cancel)(void *), void *user,
                                    const char *language) {
    HyperSttResult result;
    if (cancelled(cancel, user)) { result.status = HyperSttStatus::Cancelled; return result; }
    const bool automatic = !language || !language[0] || std::strcmp(language, "auto") == 0;
    if (!automatic && (::strnlen(language, 16) == 16 || whisper_lang_id(language) < 0)) {
        result.status = HyperSttStatus::InvalidLanguage; return result;
    }
    if (samples.size() < kMinimumSamples || samples.size() > kHyperSttMaxSamples) {
        result.status = HyperSttStatus::InvalidInput; return result;
    }
    double square_sum = 0;
    float peak = 0;
    for (float sample : samples) {
        if (!std::isfinite(sample) || sample < -1.0f || sample > 1.0f) {
            result.status = HyperSttStatus::InvalidInput; return result;
        }
        const float absolute = std::fabs(sample);
        if (absolute > peak) peak = absolute;
        square_sum += static_cast<double>(sample) * sample;
    }
    // This is only a silence gate, never a substitute for real recognition.
    if (peak < 0.005f || square_sum / samples.size() < 0.000004) {
        result.status = HyperSttStatus::Silence; return result;
    }
    if (!context_) return result;
    std::uint64_t started = 0;
    if (!now_ms(started)) { result.status = HyperSttStatus::ClockFailed; return result; }
    AbortState abort{cancel, user, started};
    auto parameters = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    parameters.n_threads = 2;
    parameters.n_max_text_ctx = 0;
    parameters.translate = false;
    parameters.no_context = true;
    parameters.no_timestamps = true;
    parameters.single_segment = true;
    parameters.print_special = false;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    parameters.token_timestamps = false;
    parameters.debug_mode = false;
    parameters.tdrz_enable = false;
    parameters.max_tokens = 96;
    parameters.initial_prompt = nullptr;
    parameters.carry_initial_prompt = false;
    parameters.prompt_tokens = nullptr;
    parameters.prompt_n_tokens = 0;
    parameters.language = automatic ? nullptr : language;
    parameters.detect_language = automatic;
    parameters.temperature = 0;
    parameters.temperature_inc = 0;
    parameters.greedy.best_of = 1;
    parameters.suppress_blank = true;
    parameters.suppress_nst = true;
    parameters.vad = false;
    parameters.vad_model_path = nullptr;
    parameters.abort_callback = AbortState::abort;
    parameters.abort_callback_user_data = &abort;
    parameters.encoder_begin_callback = AbortState::begin;
    parameters.encoder_begin_callback_user_data = &abort;
    int code = -1;
    try {
        code = whisper_full(context_, parameters, samples.data(),
                            static_cast<int>(samples.size()));
        if (abort.check()) result.status = abort.reason;
        else if (code != 0) result.status = HyperSttStatus::InferenceFailed;
        else {
            result.status = HyperSttStatus::NoSpeech;
            const int count = whisper_full_n_segments(context_);
            if (count < 0 || count > 16) result.status = HyperSttStatus::OutputLimit;
            else {
                for (int index = 0; index < count; ++index) {
                    const float no_speech = whisper_full_get_segment_no_speech_prob(context_, index);
                    if (!std::isfinite(no_speech) || no_speech >= 0.8f) continue;
                    const char *text = whisper_full_get_segment_text(context_, index);
                    if (!text) continue;
                    const std::size_t available = kHyperSttMaxTextBytes - result.text.size();
                    const std::size_t length = ::strnlen(text, available + 1);
                    if (length > available) { result.text.clear(); result.status = HyperSttStatus::OutputLimit; break; }
                    result.text.append(text, length);
                }
                if (result.status != HyperSttStatus::OutputLimit) {
                    if (!utf8_valid(result.text)) { result.text.clear(); result.status = HyperSttStatus::InferenceFailed; }
                    else {
                        trim_and_clean(result.text);
                        result.status = result.text.empty() ? HyperSttStatus::NoSpeech : HyperSttStatus::Ok;
                    }
                }
            }
        }
    } catch (const std::bad_alloc &) { result.text.clear(); result.status = HyperSttStatus::NoMemory; }
    catch (...) { result.text.clear(); result.status = HyperSttStatus::InferenceFailed; }
    std::uint64_t ended = 0;
    if (now_ms(ended) && ended >= started) result.elapsed_ms = ended - started;
    return result;
}
