#include "hyper_dialogue.hpp"
#include "hyper_locale.hpp"
#include "llama.h"
#include "ggml-cpu.h"
#include "hyper_weight_stream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <sys/stat.h>
#include <time.h>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t kMaxModelBytes = 6ull * 1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaxReplyBytes = 2048;
constexpr std::size_t kMaxPromptBytes = 16384;
constexpr unsigned kBatch = 64;
constexpr unsigned kUBatch = 32;
constexpr std::size_t kWeightCacheBytes = 512ull * 1024ull * 1024ull;

std::uint64_t now_ms() noexcept {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0)
        return UINT64_MAX;
    const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
    if (seconds > (UINT64_MAX - 999) / 1000) return UINT64_MAX;
    return seconds * 1000 + static_cast<std::uint64_t>(value.tv_nsec / 1000000);
}

bool space(unsigned char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string trimmed(const std::string &text) {
    std::size_t first = 0, last = text.size();
    while (first < last && space(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && space(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

// Return the valid prefix, distinguishing a truncated final code point.
std::size_t utf8_prefix(const std::string &text, bool &incomplete) noexcept {
    incomplete = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            if (first == 0 || (first < 0x20 && !space(first))) return i;
            ++i;
            continue;
        }
        unsigned length = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { length = 2; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { length = 3; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { length = 4; code = first & 7; minimum = 0x10000; }
        else return i;
        if (text.size() - i < length) { incomplete = true; return i; }
        for (unsigned n = 1; n < length; ++n) {
            const auto next = static_cast<unsigned char>(text[i + n]);
            if ((next & 0xc0) != 0x80) return i;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return i;
        i += length;
    }
    return i;
}

void erase_string(std::string &text) noexcept {
    volatile char *cursor = text.empty() ? nullptr : &text[0];
    for (std::size_t i = 0; i < text.size(); ++i) cursor[i] = 0;
    text.clear();
}

struct BatchOwner {
    llama_batch_ext *value;
    ~BatchOwner() { if (value) llama_batch_ext_free(value); }
};
struct SamplerOwner {
    llama_sampler *value;
    ~SamplerOwner() { if (value) llama_sampler_free(value); }
};
std::once_flag backend_once;

// Own the process-wide llama logger only during model loading. No raw library
// strings escape this scope, and every exit path restores the previous logger.
struct ModelLogScope {
    ggml_log_callback previous = nullptr;
    void *previous_data = nullptr;
    ModelLogScope(ggml_log_callback callback, void *data) noexcept {
        llama_log_get(&previous, &previous_data);
        llama_log_set(callback, data);
    }
    ~ModelLogScope() { llama_log_set(previous, previous_data); }
    ModelLogScope(const ModelLogScope &) = delete;
    ModelLogScope &operator=(const ModelLogScope &) = delete;
};

// Finite diagnostic classes; paths, metadata and arbitrary log text are never
// forwarded. These identify the library's reported failure, not a RAM verdict.
int model_error_class(const char *text) noexcept {
    if (!text) return 0;
    if (std::strstr(text, "mmap failed")) return 1;
    if (std::strstr(text, "allocate") || std::strstr(text, "allocation") ||
        std::strstr(text, "memory")) return 2;
    if (std::strstr(text, "failed to open") || std::strstr(text, "read error") ||
        std::strstr(text, "end of file") || std::strstr(text, "seek error")) return 3;
    if (std::strstr(text, "invalid data") || std::strstr(text, "invalid tensor")) return 4;
    if (std::strstr(text, "unsupported") || std::strstr(text, "unknown model")) return 5;
    return 0;
}
} // namespace

struct HyperDialogue::Impl {
    struct Turn { std::string user, assistant; };
    std::string path;
    HyperModelLoadPolicy load_policy;
    std::deque<Turn> history;
    bool pending_reply = false;
    int language = hyper_current_system_language();
    llama_model *model = nullptr;
    llama_context *context = nullptr;
    ggml_threadpool_t compute_pool = nullptr;
    hyper_weight_stream *weight_stream = nullptr;
    std::array<llama_model_tensor_buft_override, 2> weight_overrides{};
    Cancel cancel = nullptr;
    void *cancel_context = nullptr;
    std::uint64_t started = UINT64_MAX;
    HyperDialogueStatus stopped = HyperDialogueStatus::ok;
    Diagnostic diagnostic = nullptr;
    void *diagnostic_context = nullptr;
    int last_progress = -1;
    std::atomic<unsigned> load_error_records{0};

    explicit Impl(std::string value, HyperModelLoadPolicy policy)
        : path(std::move(value)), load_policy(policy) {}
    ~Impl() {
        clear_history();
        release_model();
    }

    void clear_history() noexcept {
        for (auto &turn : history) { erase_string(turn.user); erase_string(turn.assistant); }
        history.clear();
        pending_reply = false;
    }

    void begin_request(Cancel callback, void *opaque) noexcept {
        cancel = callback;
        cancel_context = opaque;
        started = now_ms();
        stopped = HyperDialogueStatus::ok;
        if (weight_stream) hyper_weight_stream_begin_request(weight_stream);
    }

    void end_request() noexcept {
        cancel = nullptr;
        cancel_context = nullptr;
        started = UINT64_MAX;
        if (context) llama_set_abort_callback(context, nullptr, nullptr);
    }

    void phase(const char *name, int progress = -1) noexcept {
        if (!diagnostic) return;
        const auto now = now_ms();
        const auto elapsed = now != UINT64_MAX && started != UINT64_MAX && now >= started ? now - started : 0;
        try { diagnostic(name, progress, elapsed, diagnostic_context); } catch (...) {}
    }

    void release_model() noexcept {
        if (context) {
            llama_set_abort_callback(context, nullptr, nullptr);
            if (auto memory = llama_get_memory(context)) llama_memory_clear(memory, true);
            if (compute_pool) llama_detach_threadpool(context);
            llama_free(context);
            context = nullptr;
        }
        // Context/backend teardown must finish before their borrowed pool dies.
        if (compute_pool) { ggml_threadpool_free(compute_pool); compute_pool = nullptr; }
        if (model) { llama_model_free(model); model = nullptr; }
        if (weight_stream) {
            hyper_weight_stream_destroy(weight_stream);
            weight_stream = nullptr;
            weight_overrides = {};
        }
    }

    void stream_diagnostics() noexcept {
        if (!weight_stream) return;
        hyper_weight_stream_stats stats{};
        hyper_weight_stream_get_stats(weight_stream, &stats);
        const auto mib = [](std::uint64_t value) {
            return static_cast<int>(std::min<std::uint64_t>(value / (1024 * 1024), INT_MAX));
        };
        phase("llm_cache_cap_mib", mib(stats.cap_bytes));
        phase("llm_cache_peak_mib", mib(stats.peak_bytes));
        // Live ranges are arena occupancy, not the process's resident memory.
        phase("llm_cache_live_mib", mib(stats.resident_bytes));
        phase("llm_cache_read_mib", mib(stats.bytes_read));
        phase("llm_cache_evictions", static_cast<int>(std::min<std::uint64_t>(stats.evictions, INT_MAX)));
        phase("llm_compute_nodes", static_cast<int>(std::min<std::uint64_t>(stats.nodes_computed, INT_MAX)));
        phase("llm_max_weight_mib", mib(stats.max_weight_bytes));
        phase("llm_cache_error", static_cast<int>(stats.error_code));
    }

    bool should_stop() noexcept {
        if (stopped != HyperDialogueStatus::ok) return true;
        try {
            if (cancel && cancel(cancel_context)) {
                stopped = HyperDialogueStatus::cancelled;
                return true;
            }
        } catch (...) {
            stopped = HyperDialogueStatus::cancelled;
            return true;
        }
        const auto now = now_ms();
        if (now == UINT64_MAX || started == UINT64_MAX || now < started ||
            now - started >= request_timeout_ms) {
            stopped = HyperDialogueStatus::timeout;
            return true;
        }
        return false;
    }

    static bool abort_callback(void *opaque) noexcept {
        return static_cast<Impl *>(opaque)->should_stop();
    }
    static void model_log(ggml_log_level level, const char *text, void *opaque) noexcept {
        if (level != GGML_LOG_LEVEL_ERROR) return;
        auto *state = static_cast<Impl *>(opaque);
        if (state->load_error_records.fetch_add(1, std::memory_order_relaxed) < 8)
            state->phase("llm_load_error", model_error_class(text));
    }
    static bool progress_callback(float value, void *opaque) noexcept {
        auto *state = static_cast<Impl *>(opaque);
        if (state->should_stop()) return false;
        // At most eleven progress records per physical model load.
        if (value >= 0.0f && value <= 1.0f) {
            const int percent = static_cast<int>(value * 10.0f) * 10;
            if (percent > state->last_progress) {
                state->last_progress = percent;
                state->phase("llm_load_progress", percent);
            }
        }
        return true;
    }

    HyperDialogueReply failure(HyperDialogueStatus status, const char *message,
                               unsigned tokens = 0) const {
        HyperDialogueReply result;
        result.status = status;
        result.error = message;
        result.generated_tokens = tokens;
        result.model_loaded = model && context;
        return result;
    }

    HyperDialogueReply load() {
        if (model && context) {
            hyper_weight_stream_stats stats{};
            hyper_weight_stream_get_stats(weight_stream, &stats);
            if (stats.error_code == HYPER_STREAM_OK)
                return failure(HyperDialogueStatus::ok, "");
        }
        release_model();
        last_progress = -1;
        load_error_records.store(0, std::memory_order_relaxed);
        phase("llm_load_begin");
        struct stat info{};
        if (path.empty() || stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
            return failure(HyperDialogueStatus::model_unavailable, "Modello chat locale assente o non leggibile.");
        if (info.st_size < 65536 || static_cast<std::uint64_t>(info.st_size) > kMaxModelBytes)
            return failure(HyperDialogueStatus::memory_limit, "Dimensione del modello fuori dal limite della prova.");
        if (should_stop()) return failure(stopped, "Caricamento chat annullato o scaduto.");
        std::call_once(backend_once, [] { llama_backend_init(); });
        auto model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;
        const bool bounded = load_policy == HyperModelLoadPolicy::bounded_stream;
        // Ordinary small models retain their existing mapped path. The two
        // 7B entries register file ranges rather than allocating all weights.
        const bool read_into_backend = bounded || load_policy == HyperModelLoadPolicy::read_into_backend;
        model_params.load_mode = read_into_backend ? LLAMA_LOAD_MODE_NONE : LLAMA_LOAD_MODE_MMAP;
        if (read_into_backend) model_params.lazy_mode = LLAMA_LAZY_MODE_OFF;
        if (bounded) {
            weight_stream = hyper_weight_stream_create(kWeightCacheBytes, abort_callback, this);
            if (!weight_stream)
                return failure(HyperDialogueStatus::memory_limit, "Cache del modello non disponibile.");
            weight_overrides[0] = {".*", hyper_weight_stream_buft(weight_stream)};
            weight_overrides[1] = {nullptr, nullptr};
            model_params.tensor_buft_overrides = weight_overrides.data();
        }
        model_params.progress_callback = progress_callback;
        model_params.progress_callback_user_data = this;
        model_params.check_tensors = true;
        model_params.use_extra_bufts = false;
        phase("llm_load_strategy", static_cast<int>(load_policy));
        {
            ModelLogScope model_logs(model_log, this);
            model = llama_model_load_from_file(path.c_str(), model_params);
        }
        if (!model) {
            stream_diagnostics();
            release_model();
            if (should_stop()) return failure(stopped, "Caricamento chat annullato o scaduto.");
            return failure(HyperDialogueStatus::model_load_failed, "Caricamento del GGUF locale non riuscito.");
        }
        if (llama_model_size(model) > kMaxModelBytes || llama_model_has_encoder(model)) {
            release_model();
            return failure(HyperDialogueStatus::memory_limit, "Modello non compatibile con questa prova CPU.");
        }
        phase("llm_weights_loaded");
        if (should_stop()) {
            release_model();
            return failure(stopped, "Caricamento chat annullato o scaduto.");
        }
        auto params = llama_context_default_params();
        params.n_ctx = context_tokens;
        params.n_batch = kBatch;
        params.n_ubatch = kUBatch;
        params.n_seq_max = 1;
        params.n_threads = threads;
        params.n_threads_batch = threads;
        params.offload_kqv = false;
        params.op_offload = false;
        params.no_perf = true;
        params.abort_callback = abort_callback;
        params.abort_callback_data = this;
        context = llama_init_from_model(model, params);
        if (!context) {
            stream_diagnostics();
            release_model();
            return failure(should_stop() ? stopped : HyperDialogueStatus::context_failed,
                           "Memoria o contesto chat non disponibili.");
        }
        if (bounded) {
            // Streaming evaluates one node at a time. Without an attached pool,
            // GGML creates and destroys a disposable pool for every node.
            auto pool_params = ggml_threadpool_params_default(threads);
            pool_params.poll = 0;
            compute_pool = ggml_threadpool_new(&pool_params);
            if (!compute_pool) {
                release_model();
                return failure(HyperDialogueStatus::memory_limit, "Thread del calcolo non disponibili.");
            }
            llama_attach_threadpool(context, compute_pool, compute_pool);
            phase("llm_compute_threads", threads);
        }
        if (should_stop()) {
            release_model();
            return failure(stopped, "Caricamento chat annullato o scaduto.");
        }
        phase("llm_context_ready");
        stream_diagnostics();
        return failure(HyperDialogueStatus::ok, "");
    }

    bool format_prompt(const std::string &input, std::size_t skipped, std::string &prompt) {
        const char *format = llama_model_chat_template(model, nullptr);
        if (!format) return false;
        std::vector<llama_chat_message> messages;
        messages.reserve(2 + (history.size() - skipped) * 2);
        const auto &locale = hyper_locale_for_system(language);
        const std::string system_prompt = std::string("You are Hyper, a friendly local assistant living in a PlayStation. ") +
            "Reply only in " + locale.name + " (" + locale.tag + "). " +
            "Follow this console language even if the user uses another language. Use the regional spelling and writing system of the locale. " +
            "Answer the actual question clearly and keep track of earlier turns. Use natural spoken sentences. " +
            "Usually use two to four sentences, fewer for a simple question. Ask for clarification when needed. " +
            "Do not invent facts or memories. Admit uncertainty. Use humor only when it fits. " +
            "Do not claim to run console commands or search online; this build can only converse.";
        messages.push_back({"system", system_prompt.c_str()});
        for (std::size_t i = skipped; i < history.size(); ++i) {
            messages.push_back({"user", history[i].user.c_str()});
            messages.push_back({"assistant", history[i].assistant.c_str()});
        }
        messages.push_back({"user", input.c_str()});
        std::array<char, kMaxPromptBytes> buffer{};
        int count = llama_chat_apply_template(format, messages.data(), messages.size(),
                                              true, buffer.data(), static_cast<int32_t>(buffer.size()));
        if (count <= 0 || static_cast<std::size_t>(count) > buffer.size()) return false;
        prompt.assign(buffer.data(), static_cast<std::size_t>(count));
        return true;
    }

    int fill_batch(llama_batch_ext *batch, const llama_token *tokens, std::size_t count,
                   std::size_t position, bool logits) {
        llama_batch_ext_clear(batch);
        for (std::size_t i = 0; i < count; ++i) {
            const auto index = llama_batch_ext_add_token(batch, 0, tokens[i]);
            const auto pos = static_cast<llama_pos>(position + i);
            if (index < 0 || !llama_batch_ext_set_pos(batch, index, &pos)) return -1;
            if (logits && i + 1 == count && !llama_batch_ext_set_output_logits(batch, index, true)) return -1;
        }
        if (should_stop()) return 2;
        return llama_process(context, LLAMA_PROCESS_TYPE_DECODE, batch);
    }

    void remember(const std::string &input, const std::string &output, std::size_t skipped = 0) {
        while (skipped-- && !history.empty()) {
            erase_string(history.front().user);
            erase_string(history.front().assistant);
            history.pop_front();
        }
        history.push_back({input, output});
        pending_reply = true;
        while (history.size() > max_history_turns) {
            erase_string(history.front().user);
            erase_string(history.front().assistant);
            history.pop_front();
        }
    }

    HyperDialogueReply run(const std::string &raw, Cancel callback, void *opaque) {
        // Legacy users do not confirm delivery. Keep their existing auto-record behavior.
        pending_reply = false;
        begin_request(callback, opaque);
        if (raw.size() > max_input_bytes)
            return failure(HyperDialogueStatus::input_too_long, "Frase troppo lunga per il contesto di prova.");
        bool incomplete = false;
        if (utf8_prefix(raw, incomplete) != raw.size() || incomplete)
            return failure(HyperDialogueStatus::invalid_input, "Testo non valido: serve una frase UTF-8.");
        const auto input = trimmed(raw);
        if (input.empty()) return failure(HyperDialogueStatus::invalid_input, "Nessuna frase ricevuta.");
        if (should_stop()) return failure(stopped, "Richiesta annullata o scaduta.");
        if (const char *answer = hyper_fast_reply(input.c_str(), language)) {
            HyperDialogueReply result;
            result.status = HyperDialogueStatus::ok;
            result.mode = HyperDialogueMode::fast;
            result.text = answer;
            result.model_loaded = model && context;
            remember(input, result.text);
            return result;
        }
        auto status = load();
        if (status.status != HyperDialogueStatus::ok) return status;
        llama_set_abort_callback(context, abort_callback, this);
        const auto *vocab = llama_model_get_vocab(model);
        if (!vocab) return failure(HyperDialogueStatus::model_load_failed, "Vocabolario del modello non disponibile.");
        std::string prompt;
        std::vector<llama_token> tokens;
        std::size_t skipped = 0;
        for (;;) {
            if (should_stop()) return failure(stopped, "Preparazione chat annullata o scaduta.");
            if (!format_prompt(input, skipped, prompt))
                return failure(HyperDialogueStatus::template_failed, "Formato conversazione del modello non supportato.");
            int required = llama_tokenize(vocab, prompt.data(), static_cast<int32_t>(prompt.size()),
                                           nullptr, 0, true, true);
            if (required >= 0 || required == INT_MIN)
                return failure(HyperDialogueStatus::template_failed, "Tokenizzazione del messaggio non riuscita.");
            required = -required;
            if (static_cast<unsigned>(required) + max_output_tokens <= context_tokens) {
                tokens.resize(static_cast<std::size_t>(required));
                int count = llama_tokenize(vocab, prompt.data(), static_cast<int32_t>(prompt.size()),
                                          tokens.data(), required, true, true);
                if (count != required) return failure(HyperDialogueStatus::template_failed, "Tokenizzazione incoerente.");
                break;
            }
            if (skipped >= history.size())
                return failure(HyperDialogueStatus::input_too_long, "Frase troppo lunga per il contesto di prova.");
            ++skipped;
        }
        erase_string(prompt);
        auto memory = llama_get_memory(context);
        if (!memory) return failure(HyperDialogueStatus::context_failed, "Memoria della conversazione non disponibile.");
        llama_memory_clear(memory, true);
        struct ClearMemory {
            llama_memory_t memory;
            ~ClearMemory() { llama_memory_clear(memory, true); }
        } clear{memory};
        BatchOwner batch{llama_batch_ext_init(context)};
        if (!batch.value) return failure(HyperDialogueStatus::context_failed, "Batch della conversazione non disponibile.");
        phase("llm_prompt_begin", static_cast<int>(tokens.size()));
        for (std::size_t position = 0; position < tokens.size(); position += kBatch) {
            const auto count = std::min<std::size_t>(kBatch, tokens.size() - position);
            if (fill_batch(batch.value, tokens.data() + position, count, position,
                           position + count == tokens.size()) != 0) {
                return failure(should_stop() ? stopped : HyperDialogueStatus::inference_failed,
                               "Elaborazione della frase non riuscita.");
            }
            phase("llm_prompt_progress", static_cast<int>(position + count));
        }
        phase("llm_prompt_complete", static_cast<int>(tokens.size()));
        stream_diagnostics();
        auto sampler_params = llama_sampler_chain_default_params();
        sampler_params.no_perf = true;
        SamplerOwner sampler{llama_sampler_chain_init(sampler_params)};
        if (!sampler.value) return failure(HyperDialogueStatus::context_failed, "Campionatore non disponibile.");
        llama_sampler_chain_add(sampler.value, llama_sampler_init_top_k(20));
        llama_sampler_chain_add(sampler.value, llama_sampler_init_top_p(0.8f, 1));
        llama_sampler_chain_add(sampler.value, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(sampler.value, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        HyperDialogueReply result;
        result.status = HyperDialogueStatus::ok;
        result.mode = HyperDialogueMode::chat;
        result.model_loaded = true;
        result.text.reserve(512);
        std::size_t position = tokens.size();
        phase("llm_decode_begin", 0);
        for (unsigned index = 0; index < max_output_tokens; ++index) {
            if (should_stop()) return failure(stopped, "Risposta annullata o scaduta.", result.generated_tokens);
            llama_token token = llama_sampler_sample(sampler.value, context, -1);
            if (token == LLAMA_TOKEN_NULL)
                return failure(HyperDialogueStatus::inference_failed, "Token della risposta non valido.", result.generated_tokens);
            if (llama_vocab_is_eog(vocab, token)) break;
            std::array<char, 512> piece{};
            int count = llama_token_to_piece(vocab, token, piece.data(), static_cast<int32_t>(piece.size()), 0, false);
            if (count < 0 || static_cast<std::size_t>(count) > piece.size())
                return failure(HyperDialogueStatus::inference_failed, "Parte della risposta non valida.", result.generated_tokens);
            if (result.text.size() + static_cast<std::size_t>(count) > kMaxReplyBytes) {
                result.truncated = true;
                break;
            }
            result.text.append(piece.data(), static_cast<std::size_t>(count));
            ++result.generated_tokens;
            if (result.generated_tokens == 1 || result.generated_tokens % 8 == 0)
                phase("llm_decode_progress", static_cast<int>(result.generated_tokens));
            if (index + 1 == max_output_tokens) { result.truncated = true; break; }
            if (fill_batch(batch.value, &token, 1, position++, true) != 0)
                return failure(should_stop() ? stopped : HyperDialogueStatus::inference_failed,
                               "Generazione della risposta non riuscita.", result.generated_tokens);
        }
        const auto prefix = utf8_prefix(result.text, incomplete);
        if (prefix != result.text.size()) {
            if (!incomplete) return failure(HyperDialogueStatus::inference_failed, "Risposta UTF-8 non valida.", result.generated_tokens);
            result.text.resize(prefix);
            result.truncated = true;
        }
        result.text = trimmed(result.text);
        if (result.text.empty()) return failure(HyperDialogueStatus::empty_output, "Il modello non ha prodotto una risposta.", result.generated_tokens);
        if (should_stop()) return failure(stopped, "Risposta annullata o scaduta.", result.generated_tokens);
        remember(input, result.text, skipped);
        return result;
    }
};

HyperDialogue::HyperDialogue(std::string model_path, HyperModelLoadPolicy policy)
    : impl_(new Impl(std::move(model_path), policy)) {}
HyperDialogue::~HyperDialogue() = default;

HyperDialogueReply HyperDialogue::reply(const std::string &input, Cancel cancel,
                                       void *cancel_context) noexcept {
    struct RequestExit {
        Impl *state;
        ~RequestExit() { state->end_request(); }
    } request_exit{impl_.get()};
    try {
        auto result = impl_->run(input, cancel, cancel_context);
        impl_->phase("llm_reply_result", static_cast<int>(result.status));
        impl_->stream_diagnostics();
        return result;
    } catch (const std::bad_alloc &) {
        HyperDialogueReply result;
        result.status = HyperDialogueStatus::memory_limit;
        result.model_loaded = model_loaded();
        return result;
    } catch (...) {
        HyperDialogueReply result;
        result.status = HyperDialogueStatus::internal_error;
        result.model_loaded = model_loaded();
        return result;
    }
}

HyperDialogueStatus HyperDialogue::prepare(Cancel cancel, void *cancel_context) noexcept {
    struct PreparationExit {
        Impl *state;
        ~PreparationExit() { state->end_request(); }
    } preparation_exit{impl_.get()};
    impl_->begin_request(cancel, cancel_context);
    try {
        if (impl_->should_stop()) return impl_->stopped;
        const auto loaded = impl_->load();
        return loaded.status;
    } catch (const std::bad_alloc &) {
        impl_->release_model();
        return HyperDialogueStatus::memory_limit;
    } catch (...) {
        impl_->release_model();
        return HyperDialogueStatus::internal_error;
    }
}

void HyperDialogue::set_diagnostic(Diagnostic diagnostic, void *context) noexcept {
    impl_->diagnostic = diagnostic;
    impl_->diagnostic_context = context;
}

void HyperDialogue::reset() noexcept {
    impl_->clear_history();
    if (impl_->context) {
        if (auto memory = llama_get_memory(impl_->context)) llama_memory_clear(memory, true);
    }
}

void HyperDialogue::set_language(int system_id) noexcept {
    const int resolved = hyper_locale_for_system(system_id).system_id;
    if (impl_->language == resolved) return;
    reset();
    impl_->language = resolved;
}

void HyperDialogue::reconcile_last_reply(bool delivered, const std::string &spoken) noexcept {
    if (!impl_->pending_reply) return;
    impl_->pending_reply = false;
    try {
        if (impl_->history.empty()) {
            impl_->clear_history();
        } else if (!delivered) {
            erase_string(impl_->history.back().user);
            erase_string(impl_->history.back().assistant);
            impl_->history.pop_back();
        } else {
            bool incomplete = false;
            if (spoken.empty() || spoken.size() > 480 ||
                utf8_prefix(spoken, incomplete) != spoken.size() || incomplete ||
                trimmed(spoken).empty()) {
                impl_->clear_history();
            } else {
                std::string replacement = spoken;
                erase_string(impl_->history.back().assistant);
                impl_->history.back().assistant.swap(replacement);
            }
        }
    } catch (...) {
        impl_->clear_history();
    }
    if (impl_->context) {
        if (auto memory = llama_get_memory(impl_->context)) llama_memory_clear(memory, true);
    }
}
bool HyperDialogue::model_loaded() const noexcept { return impl_->model && impl_->context; }
std::size_t HyperDialogue::history_turns() const noexcept { return impl_->history.size(); }

const char *hyper_dialogue_status_name(HyperDialogueStatus status) noexcept {
    switch (status) {
    case HyperDialogueStatus::ok: return "ok";
    case HyperDialogueStatus::invalid_input: return "invalid_input";
    case HyperDialogueStatus::cancelled: return "cancelled";
    case HyperDialogueStatus::timeout: return "timeout";
    case HyperDialogueStatus::model_unavailable: return "model_unavailable";
    case HyperDialogueStatus::model_load_failed: return "model_load_failed";
    case HyperDialogueStatus::memory_limit: return "memory_limit";
    case HyperDialogueStatus::context_failed: return "context_failed";
    case HyperDialogueStatus::template_failed: return "template_failed";
    case HyperDialogueStatus::input_too_long: return "input_too_long";
    case HyperDialogueStatus::inference_failed: return "inference_failed";
    case HyperDialogueStatus::empty_output: return "empty_output";
    case HyperDialogueStatus::internal_error: return "internal_error";
    }
    return "unknown";
}
