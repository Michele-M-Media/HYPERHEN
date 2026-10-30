#include "hyper_runtime_session.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <limits>
#include <new>

namespace {
template <typename Container> void erase_container(Container &value) noexcept {
    using Element = typename Container::value_type;
    auto *bytes = reinterpret_cast<volatile unsigned char *>(value.empty() ? nullptr : &value[0]);
    for (std::size_t i = 0; i < value.size() * sizeof(Element); ++i) bytes[i] = 0;
    value.clear();
}
bool cancelled(bool (*callback)(void *), void *user) noexcept {
    if (!callback) return false;
    try { return callback(user); } catch (...) { return true; }
}
bool whitespace(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
}

void hyper_worker_erase(std::vector<std::int16_t> &value) noexcept { erase_container(value); }
void hyper_worker_erase(std::vector<float> &value) noexcept { erase_container(value); }
void hyper_worker_erase(std::string &value) noexcept { erase_container(value); }

int hyper_worker_validate_done(const hyper_p14_voice_header &frame, int user,
                                std::uint64_t sequence,
                                std::uint32_t expected_samples) noexcept {
    if (user <= 0 || user == 0xfe || user == 0xff || !sequence ||
        !expected_samples || expected_samples > HYPER_P14_MAX_OUTPUT_SAMPLES ||
        frame.magic != HYPER_P14_VOICE_MAGIC || frame.version != HYPER_P14_VOICE_VERSION ||
        frame.kind != HYPER_P14_DONE || frame.user != user || frame.sequence != sequence ||
        frame.reserved != 0) return -EPROTO;
    if (frame.status < 0) {
        if (frame.samples != 0 || (frame.sample_rate != 0 && frame.sample_rate != 48000))
            return -EPROTO;
        return frame.status;
    }
    if (frame.status != 0 || frame.sample_rate != 48000 || frame.samples != expected_samples)
        return -EPROTO;
    return 0;
}

std::string hyper_worker_limit_reply(const std::string &text, std::size_t maximum) {
    if (!maximum || maximum > 480 || text.empty()) return {};
    std::size_t offset = 0, last_word = 0, boundary = 0, sentence = 0;
    bool non_ascii_prefix = false, content_seen = false;
    while (offset < text.size()) {
        const std::size_t begin = offset;
        const auto first = static_cast<unsigned char>(text[offset++]);
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first < 0x80) {
            if (!first || (first < 0x20 && !whitespace(first)) || first == 0x7f) return {};
        } else if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 7; minimum = 0x10000; }
        else return {};
        if (count > text.size() - offset) return {};
        for (unsigned i = 0; i < count; ++i) {
            const auto next = static_cast<unsigned char>(text[offset++]);
            if ((next & 0xc0) != 0x80) return {};
            code = code << 6 | (next & 63);
        }
        if (count && (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))) return {};
        if (offset <= maximum) {
            boundary = offset;
            if (first < 0x80 && (first == '.' || first == '?' || first == '!') &&
                (offset == text.size() || whitespace(static_cast<unsigned char>(text[offset]))))
                sentence = offset;
            if (count && (code == 0x3002 || code == 0xff01 || code == 0xff1f))
                sentence = offset;
            if (first < 0x80 && whitespace(first)) {
                if (content_seen) last_word = begin;
            } else {
                content_seen = true;
                if (first >= 0x80) non_ascii_prefix = true;
            }
        }
    }
    std::size_t length = text.size() <= maximum ? text.size() :
                         (sentence ? sentence : (last_word ? last_word : (non_ascii_prefix ? boundary : 0)));
    if (!length) return {};
    while (length && whitespace(static_cast<unsigned char>(text[length - 1]))) --length;
    std::size_t begin = 0;
    while (begin < length && whitespace(static_cast<unsigned char>(text[begin]))) ++begin;
    return text.substr(begin, length - begin);
}

int hyper_worker_resample_48k(const HyperTtsAudio &source,
                              std::vector<std::int16_t> &output,
                              bool (*cancel)(void *), void *user) {
    hyper_worker_erase(output);
    if (source.samples.empty() || source.sample_rate < 8000 || source.sample_rate > 96000)
        return -EINVAL;
    const std::size_t rate = static_cast<std::size_t>(source.sample_rate);
    if (source.samples.size() > rate * 40) return -E2BIG;
    const std::uint64_t scaled = static_cast<std::uint64_t>(source.samples.size()) * 48000;
    const std::size_t count = static_cast<std::size_t>((scaled + rate - 1) / rate);
    if (!count || count > HYPER_P14_MAX_OUTPUT_SAMPLES) return -E2BIG;
    if (cancelled(cancel, user)) return -ECANCELED;
    try { output.resize(count); } catch (const std::bad_alloc &) { return -ENOMEM; }
    for (std::size_t index = 0; index < count; ++index) {
        if (index % 1024 == 0 && cancelled(cancel, user)) {
            hyper_worker_erase(output); return -ECANCELED;
        }
        const std::uint64_t position = static_cast<std::uint64_t>(index) * rate;
        const std::size_t first = std::min(static_cast<std::size_t>(position / 48000), source.samples.size() - 1);
        const std::size_t second = std::min(first + 1, source.samples.size() - 1);
        const std::int64_t fraction = static_cast<std::int64_t>(position % 48000);
        const std::int64_t mixed = static_cast<std::int64_t>(source.samples[first]) * (48000 - fraction) +
                                   static_cast<std::int64_t>(source.samples[second]) * fraction;
        const std::int64_t rounded = mixed >= 0 ? (mixed + 24000) / 48000 : (mixed - 24000) / 48000;
        output[index] = static_cast<std::int16_t>(std::max<std::int64_t>(-32768, std::min<std::int64_t>(32767, rounded)));
    }
    if (cancelled(cancel, user)) { hyper_worker_erase(output); return -ECANCELED; }
    return 0;
}
