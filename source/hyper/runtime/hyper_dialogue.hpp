#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

enum class HyperDialogueMode { fast, chat };

enum class HyperModelLoadPolicy { mapped = 0, read_into_backend = 1, bounded_stream = 2 };

// Only the two 7B catalogue entries use a bounded disk-backed weight cache.
constexpr HyperModelLoadPolicy hyper_model_load_policy(int model_id) noexcept {
    return model_id == 6 || model_id == 7
        ? HyperModelLoadPolicy::bounded_stream : HyperModelLoadPolicy::mapped;
}

enum class HyperDialogueStatus {
    ok,
    invalid_input,
    cancelled,
    timeout,
    model_unavailable,
    model_load_failed,
    memory_limit,
    context_failed,
    template_failed,
    input_too_long,
    inference_failed,
    empty_output,
    internal_error
};

struct HyperDialogueReply {
    HyperDialogueStatus status = HyperDialogueStatus::internal_error;
    HyperDialogueMode mode = HyperDialogueMode::chat;
    std::string text;
    std::string error;
    unsigned generated_tokens = 0;
    bool truncated = false;
    bool model_loaded = false;
};

// Use one instance from one worker thread. Callback true means cancel.
// No audio, network, file writes or system commands are performed here.
class HyperDialogue {
public:
    using Cancel = bool (*)(void *);
    // Fixed phase names and numeric progress only; never receives conversation text.
    using Diagnostic = void (*)(const char *, int, std::uint64_t, void *);
    static constexpr std::size_t max_input_bytes = 1024;
    static constexpr unsigned max_output_tokens = 160;
    static constexpr unsigned context_tokens = 4096;
    static constexpr unsigned threads = 4;
    static constexpr unsigned max_history_turns = 8;
    static constexpr std::uint64_t request_timeout_ms = 90000;

    explicit HyperDialogue(std::string model_path = "/data/Hyper/models/hyper-chat.gguf",
                           HyperModelLoadPolicy policy = HyperModelLoadPolicy::mapped);
    ~HyperDialogue();
    HyperDialogue(const HyperDialogue &) = delete;
    HyperDialogue &operator=(const HyperDialogue &) = delete;
    HyperDialogue(HyperDialogue &&) = delete;
    HyperDialogue &operator=(HyperDialogue &&) = delete;

    HyperDialogueReply reply(const std::string &input, Cancel cancel = nullptr,
                             void *cancel_context = nullptr) noexcept;
    // Initializes weights and context without adding a conversational turn.
    // Its timeout is independent of every later reply() request.
    HyperDialogueStatus prepare(Cancel cancel = nullptr, void *cancel_context = nullptr) noexcept;
    void set_diagnostic(Diagnostic diagnostic, void *context = nullptr) noexcept;
    void set_language(int system_id) noexcept;
    // Resolve the last successful reply once, after playback/cleanup completes.
    // A later reply() without reconciliation accepts the prior text for legacy callers.
    void reconcile_last_reply(bool delivered, const std::string &spoken) noexcept;
    void reset() noexcept;
    bool model_loaded() const noexcept;
    std::size_t history_turns() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char *hyper_dialogue_status_name(HyperDialogueStatus status) noexcept;
