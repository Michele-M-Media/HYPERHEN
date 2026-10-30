#include "hyper_stt.hpp"
#include "hyper_dialogue.hpp"
#include "hyper_startup_ready.hpp"
#include "hyper_tts.hpp"
#include "hyper_reply_tts.hpp"
#include <onion/hyper_model_catalog.hpp>
#include <onion/hyper_runtime_selection.hpp>
#include <onion/hyper_assets.hpp>
#include <onion/hyper_voice_catalog.hpp>
#include "hyper_runtime_session.hpp"
#include "hyper_locale.hpp"
#include "hyper_stop.hpp"
#include "hyper_turn_input.hpp"
#include "onion/hyper_lifecycle.hpp"

extern "C" {
#include "activation_gate.h"
#include "native_input.h"
#include "native_input_wire.h"
#include "hyper_voice_wire.h"
}

#include <cstdio>

#ifndef HYPER_P5_PS5
int main() {
    std::fprintf(stderr, "Hyper worker requires PS5 credential APIs; use separate host smoke tests.\n");
    return 2;
}
#else
#include <ps5/klog.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr const char *kLockPath = onion::hyper::kWorkerLock;
constexpr char kTtsPath[] = "/data/Hyper/espeak-ng-data";
constexpr std::uint64_t kTurnLimitMs = 240000;
int active_model_id = onion::hyper::kDefaultModelId;
constexpr std::uint64_t kAcceptLimitMs = 15000;

std::atomic<bool> done{false};
std::atomic<std::uint64_t> watchdog_deadline{0};
std::uint64_t launch_token = 0;

bool stopping() noexcept {
    return hyper_stop_requested() || done.load(std::memory_order_acquire);
}
bool stop_callback(void *) { return stopping(); }

std::uint64_t now_ms() noexcept {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return UINT64_MAX;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000u +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000000u;
}
void delay_ms(unsigned ms) {
    timespec wait{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
    while (nanosleep(&wait, &wait) != 0 && errno == EINTR) {}
}
void log(const char *format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    (void)klog_printf("[Hyper P14 worker] %s\n", buffer);
}
template <typename Function> Function resident_symbol(const char *name) {
    static_assert(sizeof(Function) == sizeof(void *));
    Function result = nullptr;
    void *address = dlsym(RTLD_DEFAULT, name);
    if (address) std::memcpy(&result, &address, sizeof(result));
    return result;
}
using ProcessName = int (*)(int, char *);
ProcessName process_name = nullptr;

bool notice(const char *text) {
    struct Notification { char reserved[45]; char message[3075]; } request{};
    const auto send = resident_symbol<int (*)(int, void *, size_t, int)>("sceKernelSendNotificationRequest");
    if (!send) { log("notification_api_unavailable"); return false; }
    (void)snprintf(request.message, sizeof(request.message), "[Hyper] %s", text);
    return send(0, &request, sizeof(request), 0) >= 0;
}

struct Descriptor {
    int fd = -1;
    Descriptor() = default;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) (void)close(fd); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};

struct ReadyMarker {
    bool owned = false;
    ~ReadyMarker() {
        if (!owned) return;
        Descriptor marker(open(onion::hyper::kWorkerMarker, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
        char contents[128]{};
        const auto count = marker.fd >= 0 ? read(marker.fd, contents, sizeof(contents) - 1) : -1;
        onion::hyper::WorkerMarker parsed;
        if (count > 0 && static_cast<size_t>(count) == std::strlen(contents) &&
            onion::hyper::parse_worker_marker(contents, &parsed) &&
            onion::hyper::worker_marker_matches(parsed, getpid(), launch_token))
            (void)unlink(onion::hyper::kWorkerMarker);
    }
    bool publish() {
        if (!launch_token) return true; // Preserve direct standalone invocation.
        char temporary[128]{}, contents[128]{};
        (void)snprintf(temporary, sizeof(temporary), "%s.%d.tmp",
                       onion::hyper::kWorkerMarker, getpid());
        const int count = snprintf(contents, sizeof(contents), "hyper-r34 34 %d %llu",
                       getpid(), static_cast<unsigned long long>(launch_token));
        if (count <= 0 || static_cast<size_t>(count) >= sizeof(contents)) return false;
        Descriptor marker(open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (marker.fd < 0) return false;
        if (write(marker.fd, contents, static_cast<size_t>(count)) != count ||
            fsync(marker.fd) != 0 || rename(temporary, onion::hyper::kWorkerMarker) != 0) {
            (void)unlink(temporary); return false;
        }
        owned = true;
        return true;
    }
};
bool nonblocking(int fd) {
    return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0;
}

struct Input {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    HyperTurnInputState state;
} input;

bool snapshot(hyper_p5_sample &sample, std::uint64_t &generation) {
    if (pthread_mutex_lock(&input.mutex) != 0) return false;
    const std::uint64_t now = now_ms();
    sample = input.state.sample; generation = input.state.generation;
    const bool fresh = input.state.fresh(now) && hyper_p5_user_valid(sample.user);
    (void)pthread_mutex_unlock(&input.mutex);
    return fresh;
}

void *input_loop(void *) {
    // This is the only consumer of native_input_socket and the only writer of
    // the mutex-protected snapshot, including during heavy inference calls.
    while (!stopping()) {
        hyper_p5_sample sample{};
        const int code = hyper_p5_native_read_input(&sample);
        const std::uint64_t now = now_ms();
        if (pthread_mutex_lock(&input.mutex) == 0) {
            input.state.observe(sample, now, code);
            (void)pthread_mutex_unlock(&input.mutex);
        }
        delay_ms(10);
    }
    return nullptr;
}

void *supervise(void *) {
    // Only this standalone process is terminated. Never signal ShellUI or
    // attempt loader/global-audio cleanup from the supervisor thread.
    while (!done.load(std::memory_order_acquire)) {
        const std::uint64_t deadline = watchdog_deadline.load(std::memory_order_acquire);
        const std::uint64_t now = now_ms();
        if (now == UINT64_MAX || (deadline && now >= deadline)) _exit(4);
        delay_ms(20);
    }
    return nullptr;
}

struct Turn {
    int user;
    std::uint64_t sequence, generation, deadline;
    mutable std::atomic<bool> cancellation_logged{false};
    bool cancelled() const {
        const std::uint64_t now = now_ms();
        HyperTurnInputReason why = HyperTurnInputReason::none;
        std::uint64_t age = UINT64_MAX, current = generation;
        bool processing = false;
        if (stopping()) why = HyperTurnInputReason::stopping;
        else if (now == UINT64_MAX) why = HyperTurnInputReason::clock;
        else if (now >= deadline) why = HyperTurnInputReason::deadline;
        else if (pthread_mutex_lock(&input.mutex) != 0) why = HyperTurnInputReason::mutex;
        else {
            // Sample time under the same lock as the snapshot: input may have
            // advanced while acquiring the lock, without any clock rollback.
            const auto sampled_now = now_ms();
            why = sampled_now != UINT64_MAX && sampled_now >= deadline
                ? HyperTurnInputReason::deadline : input.state.evaluate(user, generation, sampled_now);
            age = input.state.age(sampled_now); current = input.state.generation;
            processing = input.state.processing;
            (void)pthread_mutex_unlock(&input.mutex);
        }
        if (why == HyperTurnInputReason::none) return false;
        if (!cancellation_logged.exchange(true, std::memory_order_acq_rel))
            log("turn_cancel reason=%u input_age_ms=%llu generation=%llu expected=%llu processing=%d",
                static_cast<unsigned>(why), static_cast<unsigned long long>(age),
                static_cast<unsigned long long>(current), static_cast<unsigned long long>(generation), processing);
        return true;
    }
    static bool cancel(void *value) { return static_cast<Turn *>(value)->cancelled(); }
};

struct ProcessingInputScope {
    bool enabled = false;
    explicit ProcessingInputScope(const Turn &turn) {
        if (pthread_mutex_lock(&input.mutex) != 0) return;
        if (input.state.evaluate(turn.user, turn.generation, now_ms()) == HyperTurnInputReason::none) {
            input.state.processing = true;
            enabled = true;
        }
        (void)pthread_mutex_unlock(&input.mutex);
    }
    bool reset() {
        if (!enabled) return true;
        if (pthread_mutex_lock(&input.mutex) == 0) {
            input.state.processing = false;
            (void)pthread_mutex_unlock(&input.mutex);
            enabled = false;
            return true;
        }
        return false;
    }
    ~ProcessingInputScope() { reset(); }
    int before_output(Turn &turn) {
        const auto start = now_ms();
        if (start == UINT64_MAX) return -EIO;
        for (;;) {
            if (turn.cancelled()) return -ECANCELED;
            hyper_p5_sample sample{}; std::uint64_t generation = 0;
            if (snapshot(sample, generation) && sample.user == turn.user && generation == turn.generation) {
                if (!reset()) return -EIO;
                return turn.cancelled() ? -ECANCELED : 0;
            }
            const auto now = now_ms();
            if (now == UINT64_MAX || now < start) return -EIO;
            if (now-start >= HyperTurnInputState::processing_ms) return -ETIMEDOUT;
            delay_ms(10);
        }
    }
};

int await(int fd, short events, Turn &turn, std::uint64_t deadline) {
    for (;;) {
        if (turn.cancelled()) return -ECANCELED;
        const std::uint64_t now = now_ms();
        if (now == UINT64_MAX) return -EIO;
        if (now >= deadline) return -ETIMEDOUT;
        pollfd waiter{fd, events, 0};
        const int code = poll(&waiter, 1, static_cast<int>(std::min<std::uint64_t>(25, deadline - now)));
        if (code < 0) { if (errno == EINTR) continue; return -(errno ? errno : EIO); }
        if (!code) continue;
        if (waiter.revents & (POLLERR | POLLNVAL)) return -EIO;
        if (waiter.revents & events) return 0;
        if (waiter.revents & POLLHUP) return -EPIPE;
    }
}
int read_exact(int fd, void *destination, size_t size, Turn &turn, std::uint64_t deadline) {
    auto *bytes = static_cast<unsigned char *>(destination);
    size_t used = 0;
    while (used < size) {
        const int ready = await(fd, POLLIN, turn, deadline);
        if (ready < 0) return ready;
        const ssize_t got = recv(fd, bytes + used, size - used, MSG_DONTWAIT);
        if (got > 0) used += static_cast<size_t>(got);
        else if (!got) return -EPIPE;
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            return -(errno ? errno : EIO);
    }
    return turn.cancelled() ? -ECANCELED : 0;
}
int write_exact(int fd, const void *source, size_t size, Turn &turn) {
    const auto *bytes = static_cast<const unsigned char *>(source);
    size_t used = 0;
    while (used < size) {
        const int ready = await(fd, POLLOUT, turn, turn.deadline);
        if (ready < 0) return ready;
        const ssize_t sent = send(fd, bytes + used, size - used, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) used += static_cast<size_t>(sent);
        else if (!sent) return -EPIPE;
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            return -(errno ? errno : EIO);
    }
    return turn.cancelled() ? -ECANCELED : 0;
}

hyper_p14_voice_header header(std::uint32_t kind, const Turn &turn) {
    hyper_p14_voice_header frame{};
    frame.magic = HYPER_P14_VOICE_MAGIC; frame.version = HYPER_P14_VOICE_VERSION;
    frame.kind = kind; frame.user = turn.user; frame.sequence = turn.sequence;
    return frame;
}
bool identity(const hyper_p14_voice_header &frame, std::uint32_t kind, const Turn &turn) {
    return frame.magic == HYPER_P14_VOICE_MAGIC && frame.version == HYPER_P14_VOICE_VERSION &&
           frame.kind == kind && frame.user == turn.user && frame.sequence == turn.sequence &&
           frame.reserved == 0;
}

bool shellui_credentials(const msghdr &message) {
    if (!process_name || (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC))) return false;
    // Ancillary metadata is provided by the kernel, never read from PCM bytes.
    auto *mutable_message = const_cast<msghdr *>(&message);
    for (cmsghdr *control = CMSG_FIRSTHDR(mutable_message); control;
         control = CMSG_NXTHDR(mutable_message, control)) {
        if (control->cmsg_level != SOL_SOCKET || control->cmsg_type != SCM_CREDS ||
            control->cmsg_len < CMSG_LEN(sizeof(cmsgcred))) continue;
        cmsgcred credentials{};
        std::memcpy(&credentials, CMSG_DATA(control), sizeof(credentials));
        if (credentials.cmcred_pid <= 1) return false;
        char name[64]{};
        if (process_name(credentials.cmcred_pid, name) < 0) return false;
        name[sizeof(name) - 1] = '\0';
        return std::strcmp(name, "SceShellUI") == 0;
    }
    return false;
}

int send_ack(int fd, Turn &turn) {
    auto frame = header(HYPER_P14_HELLO, turn);
    for (;;) {
        const int ready = await(fd, POLLOUT, turn, turn.deadline);
        if (ready < 0) return ready;
        iovec data{&frame, sizeof(frame)};
        alignas(cmsghdr) unsigned char ancillary[CMSG_SPACE(sizeof(cmsgcred))]{};
        msghdr message{};
        message.msg_iov = &data; message.msg_iovlen = 1;
        message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
        cmsghdr *credentials = CMSG_FIRSTHDR(&message);
        if (!credentials) return -EIO;
        credentials->cmsg_len = CMSG_LEN(sizeof(cmsgcred));
        credentials->cmsg_level = SOL_SOCKET; credentials->cmsg_type = SCM_CREDS;
        const ssize_t sent = sendmsg(fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            if (sent > static_cast<ssize_t>(sizeof(frame))) return -EIO;
            return write_exact(fd, reinterpret_cast<const unsigned char *>(&frame) + sent,
                               sizeof(frame) - static_cast<size_t>(sent), turn);
        }
        if (!sent) return -EPIPE;
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            return -(errno ? errno : EIO);
    }
}

int receive_hello(int fd, Turn &turn, std::uint64_t deadline) {
    hyper_p14_voice_header frame{};
    for (;;) {
        const int ready = await(fd, POLLIN, turn, deadline);
        if (ready < 0) return ready;
        iovec data{&frame, sizeof(frame)};
        alignas(cmsghdr) unsigned char ancillary[CMSG_SPACE(sizeof(cmsgcred))]{};
        msghdr message{};
        message.msg_iov = &data; message.msg_iovlen = 1;
        message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
        const ssize_t got = recvmsg(fd, &message, MSG_DONTWAIT);
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -(errno ? errno : EIO);
        }
        if (!got) return -EPIPE;
        if (got > static_cast<ssize_t>(sizeof(frame)) || !shellui_credentials(message)) return -EACCES;
        if (got < static_cast<ssize_t>(sizeof(frame))) {
            const int code = read_exact(fd, reinterpret_cast<unsigned char *>(&frame) + got,
                                        sizeof(frame) - static_cast<size_t>(got), turn, deadline);
            if (code < 0) return code;
        }
        if (!identity(frame, HYPER_P14_HELLO, turn) || frame.samples ||
            frame.sample_rate || frame.status) return -EPROTO;
        return send_ack(fd, turn);
    }
}

struct Server {
    Descriptor socket;
    bool owned = false;
    ~Server() { if (owned) (void)unlink(HYPER_P14_VOICE_SOCKET); }
    int start() {
        socket.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket.fd < 0) return -(errno ? errno : EIO);
        if (!nonblocking(socket.fd)) return -(errno ? errno : EIO);
        struct stat existing{};
        if (lstat(HYPER_P14_VOICE_SOCKET, &existing) == 0) {
            if (!S_ISSOCK(existing.st_mode) || existing.st_uid != geteuid() ||
                unlink(HYPER_P14_VOICE_SOCKET) != 0) return -EEXIST;
        } else if (errno != ENOENT) return -(errno ? errno : EIO);
        sockaddr_un address{}; address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, HYPER_P14_VOICE_SOCKET, sizeof(HYPER_P14_VOICE_SOCKET));
        if (bind(socket.fd, reinterpret_cast<sockaddr *>(&address), SUN_LEN(&address)) != 0)
            return -(errno ? errno : EIO);
        owned = true;
        if (chmod(HYPER_P14_VOICE_SOCKET, 0622) != 0 || listen(socket.fd, 2) != 0)
            return -(errno ? errno : EIO);
        return 0;
    }
    int accept_shellui(Turn &turn) {
        const std::uint64_t now = now_ms();
        if (now == UINT64_MAX) return -EIO;
        const std::uint64_t deadline = std::min(turn.deadline, now + kAcceptLimitMs);
        for (;;) {
            const int ready = await(socket.fd, POLLIN, turn, deadline);
            if (ready < 0) return ready;
            const int client = accept(socket.fd, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return -(errno ? errno : EIO);
            }
            if (!nonblocking(client)) { (void)close(client); continue; }
            const int authenticated = receive_hello(client, turn, deadline);
            if (authenticated == 0) return client;
            (void)close(client);
            if (authenticated == -ECANCELED || authenticated == -ETIMEDOUT) return authenticated;
            log("voice_peer_rejected code=%d", authenticated);
        }
    }
};

int request_capture(Turn &turn) {
    if (turn.cancelled()) return -ECANCELED;
    Descriptor socket(::socket(AF_UNIX, SOCK_DGRAM, 0));
    if (socket.fd < 0) return -(errno ? errno : EIO);
    (void)fcntl(socket.fd, F_SETFD, FD_CLOEXEC);
    sockaddr_un destination{}; destination.sun_family = AF_UNIX;
    std::memcpy(destination.sun_path, HYPER_P5_AUDIO_SOCKET, sizeof(HYPER_P5_AUDIO_SOCKET));
    hyper_p5_audio_command command{HYPER_P5_AUDIO_MAGIC, HYPER_P5_AUDIO_VERSION,
                                  HYPER_P14_AUDIO_REQUEST, turn.user, turn.sequence};
    iovec data{&command, sizeof(command)};
    alignas(cmsghdr) unsigned char ancillary[CMSG_SPACE(sizeof(cmsgcred))]{};
    msghdr message{};
    message.msg_name = &destination; message.msg_namelen = SUN_LEN(&destination);
    message.msg_iov = &data; message.msg_iovlen = 1;
    message.msg_control = ancillary; message.msg_controllen = sizeof(ancillary);
    cmsghdr *credentials = CMSG_FIRSTHDR(&message);
    if (!credentials) return -EIO;
    credentials->cmsg_len = CMSG_LEN(sizeof(cmsgcred));
    credentials->cmsg_level = SOL_SOCKET; credentials->cmsg_type = SCM_CREDS;
    const ssize_t sent = sendmsg(socket.fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(sizeof(command)) ? 0 : -(errno ? errno : EIO);
}

struct Sensitive {
    std::vector<std::int16_t> capture, output;
    std::vector<float> float_pcm;
    HyperTtsAudio native_tts;
    std::string transcript, reply;
    ~Sensitive() {
        hyper_worker_erase(capture); hyper_worker_erase(output); hyper_worker_erase(float_pcm);
        hyper_tts_clear(native_tts); hyper_worker_erase(transcript); hyper_worker_erase(reply);
    }
};

struct PendingReply {
    HyperDialogue &dialogue;
    bool settled = false;
    ~PendingReply() {
        // Every early return and exception after a successful decoder result
        // rolls back this turn unless the client confirms real playback DONE.
        if (!settled) dialogue.reconcile_last_reply(false, std::string{});
    }
    void delivered(const std::string &spoken) noexcept {
        dialogue.reconcile_last_reply(true, spoken);
        settled = true;
    }
};

int send_error(int fd, Turn &turn, int status) {
    auto frame = header(HYPER_P14_REPLY, turn);
    frame.status = status < 0 ? status : -EIO;
    return write_exact(fd, &frame, sizeof(frame), turn);
}

int run_turn(Server &server, HyperStt &stt, HyperDialogue &dialogue, Turn &turn) {
    const auto &locale = hyper_locale_for_system(hyper_current_system_language());
    dialogue.set_language(locale.system_id);
    log("locale system_id=%d tag=%s", locale.system_id, locale.tag);
    onion::hyper::RuntimeSelection selection;
    int selected_voice = 0;
    if (onion::hyper::read_selection(selection) &&
        selection.model_id == active_model_id && onion::hyper::voice_entry(selection.voice_id))
        selected_voice = selection.voice_id;
    log("catalog model_id=%d voice_id=%d", active_model_id, selected_voice);
    int code = request_capture(turn);
    if (code < 0) return code;
    Descriptor client(server.accept_shellui(turn));
    if (client.fd < 0) return client.fd;
    Sensitive private_data;
    auto fail = [&](int status) {
        if (!turn.cancelled()) (void)send_error(client.fd, turn, status);
        return status;
    };
    try {
        hyper_p14_voice_header capture{};
        code = read_exact(client.fd, &capture, sizeof(capture), turn, turn.deadline);
        if (code < 0) return code;
        if (!identity(capture, HYPER_P14_CAPTURE, turn) || capture.sample_rate != 16000 ||
            !capture.samples || capture.samples > HYPER_P14_MAX_INPUT_SAMPLES || capture.status)
            return fail(-EPROTO);
        private_data.capture.resize(capture.samples);
        code = read_exact(client.fd, private_data.capture.data(),
                          private_data.capture.size() * sizeof(std::int16_t), turn, turn.deadline);
        if (code < 0) return code;
        ProcessingInputScope processing(turn);
        if (!processing.enabled) return -ECANCELED;
        private_data.float_pcm.resize(private_data.capture.size());
        for (size_t i = 0; i < private_data.capture.size(); ++i)
            private_data.float_pcm[i] = static_cast<float>(private_data.capture[i]) / 32768.0f;
        hyper_worker_erase(private_data.capture);
        auto recognized = stt.transcribe(private_data.float_pcm, Turn::cancel, &turn,
                                         locale.whisper_code);
        hyper_worker_erase(private_data.float_pcm);
        log("stt status=%s elapsed_ms=%llu", hyper_stt_status_name(recognized.status),
            static_cast<unsigned long long>(recognized.elapsed_ms));
        if (recognized.status != HyperSttStatus::Ok) {
            hyper_worker_erase(recognized.text);
            if (recognized.status == HyperSttStatus::Silence || recognized.status == HyperSttStatus::NoSpeech) {
                (void)notice(hyper_message(HyperMessage::interrupted, locale.system_id));
                return fail(-ENOMSG);
            }
            return fail(recognized.status == HyperSttStatus::Cancelled ? -ECANCELED : -EIO);
        }
        private_data.transcript = recognized.text;
        hyper_worker_erase(recognized.text);
        if (turn.cancelled()) return -ECANCELED;
        auto response = dialogue.reply(private_data.transcript, Turn::cancel, &turn);
        hyper_worker_erase(private_data.transcript);
        log("dialogue status=%s mode=%s tokens=%u", hyper_dialogue_status_name(response.status),
            response.mode == HyperDialogueMode::fast ? "fast" : "chat", response.generated_tokens);
        if (response.status != HyperDialogueStatus::ok) {
            hyper_worker_erase(response.text); hyper_worker_erase(response.error);
            return fail(response.status == HyperDialogueStatus::cancelled ? -ECANCELED : -EIO);
        }
        PendingReply pending_reply{dialogue};
        private_data.reply = hyper_worker_limit_reply(response.text);
        hyper_worker_erase(response.text); hyper_worker_erase(response.error);
        if (private_data.reply.empty()) return fail(-ENOMSG);
        if (turn.cancelled()) return -ECANCELED;
        const std::uint64_t tts_started = now_ms();
        code = hyper_reply_tts(private_data.reply, private_data.native_tts, locale.voice,
                               selected_voice, Turn::cancel, &turn, kTtsPath);
        if (code < 0) return fail(code);
        code = hyper_worker_resample_48k(private_data.native_tts, private_data.output, Turn::cancel, &turn);
        hyper_tts_clear(private_data.native_tts);
        if (code < 0) return fail(code);
        auto reply = header(HYPER_P14_REPLY, turn);
        reply.sample_rate = 48000; reply.samples = static_cast<std::uint32_t>(private_data.output.size());
        if (!reply.samples || reply.samples > HYPER_P14_MAX_OUTPUT_SAMPLES) return fail(-E2BIG);
        code = processing.before_output(turn);
        if (code < 0) return fail(code);
        code = write_exact(client.fd, &reply, sizeof(reply), turn);
        if (code >= 0) code = write_exact(client.fd, private_data.output.data(), private_data.output.size() * sizeof(std::int16_t), turn);
        const std::uint64_t finished = now_ms();
        log("reply status=%d samples=%u tts_and_transfer_ms=%llu", code, reply.samples,
            static_cast<unsigned long long>(finished >= tts_started ? finished - tts_started : 0));
        if (code >= 0) {
            // The authenticated connection stays open through native playback,
            // drain and cleanup. A time delay is never a completion signal.
            if (finished == UINT64_MAX) return -EIO;
            const std::uint64_t ack_deadline = std::min(turn.deadline, finished +
                (static_cast<std::uint64_t>(reply.samples) * 1000 + 47999) / 48000 + 5000);
            hyper_p14_voice_header completed{};
            code = read_exact(client.fd, &completed, sizeof(completed), turn, ack_deadline);
            if (code >= 0)
                code = hyper_worker_validate_done(completed, turn.user, turn.sequence, reply.samples);
            log("playback_done_ack status=%d samples=%u", code, code == 0 ? completed.samples : 0);
            if (code == 0) pending_reply.delivered(private_data.reply);
        }
        return code;
    } catch (const std::bad_alloc &) { return fail(-ENOMEM); }
    catch (...) { return fail(-EIO); }
}

bool required_assets() {
    struct stat file{};
    if (!onion::hyper::model_entry(active_model_id) ||
        !onion::hyper::assets_ready_for_model(active_model_id)) {
        (void)notice(hyper_message(HyperMessage::assets_missing));
        log("chat_asset_unavailable"); return false;
    }
    if (stat(kTtsPath, &file) != 0 || !S_ISDIR(file.st_mode)) {
        (void)notice(hyper_message(HyperMessage::assets_missing));
        log("tts_asset_unavailable"); return false;
    }
    return true;
}

int assistant_loop() {
    HyperStt stt;
    const std::uint64_t load_started = now_ms();
    const auto loaded = stt.load(kHyperSttModelPath, stop_callback, nullptr);
    log("stt_load status=%s elapsed_ms=%llu", hyper_stt_status_name(loaded),
        static_cast<unsigned long long>(now_ms() >= load_started ? now_ms() - load_started : 0));
    if (loaded != HyperSttStatus::Ok) {
        if (stopping()) return 0;
        (void)notice(hyper_message(HyperMessage::assets_missing));
        return 3;
    }
    if (!required_assets()) return 3;
    process_name = resident_symbol<ProcessName>("sceKernelGetProcessName");
    if (!process_name) { log("process_identity_api_unavailable"); (void)notice(hyper_message(HyperMessage::interrupted)); return 3; }
    const std::string chat_path = onion::hyper::model_primary_path(active_model_id);
    HyperDialogue dialogue(chat_path, hyper_model_load_policy(active_model_id));
    dialogue.set_diagnostic([](const char *phase, int progress, std::uint64_t elapsed, void *) {
        log("%s model=%d progress=%d elapsed_ms=%llu", phase, active_model_id, progress,
            static_cast<unsigned long long>(elapsed));
    });
    Server server;
    ReadyMarker ready_marker;
    const std::uint64_t preload_started = now_ms();
    const auto startup = hyper_initialize_before_ready(
        [&]() { return dialogue.prepare(stop_callback, nullptr); }, stopping,
        [&]() {
            const int listening = server.start();
            if (listening < 0) { log("voice_server_failed code=%d", listening); return false; }
            if (stopping()) return false;
            if (!ready_marker.publish()) { log("ready_marker_failed"); return false; }
            if (!launch_token && !notice(hyper_message(HyperMessage::ready))) return false;
            log("READY background=1 auto_listen=0");
            watchdog_deadline.store(0, std::memory_order_release);
            return true;
        });
    log("llm_preload_result status=%s model=%d elapsed_ms=%llu",
        hyper_dialogue_status_name(startup), active_model_id,
        static_cast<unsigned long long>(now_ms() >= preload_started ? now_ms() - preload_started : 0));
    if (startup != HyperDialogueStatus::ok) {
        if (stopping()) return 0;
        const auto message = startup == HyperDialogueStatus::memory_limit || startup == HyperDialogueStatus::context_failed
            ? HyperMessage::low_memory : startup == HyperDialogueStatus::model_unavailable
            ? HyperMessage::assets_missing : HyperMessage::interrupted;
        (void)notice(hyper_message(message));
        return 3;
    }

    hyper_p5_gate gate{};
    bool have_gate = false;
    int previous_user = -1;
    std::uint64_t last_started_sequence = 0;
    while (!stopping()) {
        hyper_p5_sample sample{}; std::uint64_t generation = 0;
        const std::uint64_t now = now_ms();
        if (now == UINT64_MAX) return 3;
        if (!snapshot(sample, generation)) { have_gate = false; delay_ms(10); continue; }
        if (previous_user != sample.user) {
            dialogue.reset(); previous_user = sample.user; have_gate = false;
        }
        if (!have_gate) {
            have_gate = hyper_p5_gate_init(&gate, sample.user, now);
            if (!have_gate) { delay_ms(10); continue; }
        }
        const auto event = hyper_p5_gate_step(&gate, now, &sample);
        if (event == HYPER_P5_START && sample.sequence != last_started_sequence) {
            last_started_sequence = sample.sequence;
            Turn turn{sample.user, sample.sequence, generation, now + kTurnLimitMs};
            watchdog_deadline.store(turn.deadline, std::memory_order_release);
            log("turn_start");
            const int result = run_turn(server, stt, dialogue, turn);
            watchdog_deadline.store(0, std::memory_order_release);
            log("turn_finished status=%d elapsed_ms=%llu", result,
                static_cast<unsigned long long>(now_ms() >= now ? now_ms() - now : 0));
            if (result < 0 && result != -ECANCELED && result != -ENOMSG)
                (void)notice(hyper_message(HyperMessage::interrupted));
            hyper_p5_gate_finish(&gate); have_gate = false;
        } else if (event == HYPER_P5_EXPIRED || event == HYPER_P5_CANCEL) have_gate = false;
        delay_ms(10);
    }
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    log("ENTRY revision=45 argc=%d", argc);
    // File-based launches inherit the ELF basename from the private loader.
    // Restore the exact P14 identity before READY or authenticated audio use.
    errno = 0;
    const long renamed = syscall(SYS_thr_set_name, -1, "SceSpZeroConf");
    if (renamed != 0) {
        log("identity_rename_failed rc=%ld errno=%d", renamed, errno);
        return 5;
    }
    process_name = resident_symbol<ProcessName>("sceKernelGetProcessName");
    if (!process_name) { log("process_identity_api_unavailable phase=entry"); return 5; }
    char identity_name[64]{};
    const int identity_status = process_name(getpid(), identity_name);
    identity_name[sizeof(identity_name) - 1] = '\0';
    if (identity_status != 0 || std::strcmp(identity_name, "SceSpZeroConf") != 0) {
        log("identity_verification_failed rc=%d", identity_status);
        return 5;
    }
    log("identity status=ok expected=SceSpZeroConf");
    if (!hyper_install_stop_handler()) { log("stop_handler_failed"); return 5; }
    if (argc != 1) {
        if (argc != 3 && argc != 5) { log("arguments_rejected reason=argc"); return 5; }
        if (!argv || !argv[1] || std::strcmp(argv[1], "--hyperhen-r34") != 0) {
            log("arguments_rejected reason=launch_option"); return 5;
        }
        if (!argv[2] || argv[2][0] < '1' || argv[2][0] > '9') {
            log("arguments_rejected reason=launch_token"); return 5;
        }
        char *end = nullptr;
        errno = 0;
        launch_token = std::strtoull(argv[2], &end, 10);
        if (errno || !launch_token || !end || *end != '\0') {
            log("arguments_rejected reason=launch_token"); return 5;
        }
        if (argc == 5) {
            if (!argv[3] || std::strcmp(argv[3], "--hyper-model") != 0) {
                log("arguments_rejected reason=model_option"); return 5;
            }
            if (!argv[4] || argv[4][0] < '0' || argv[4][0] > '9') {
                log("arguments_rejected reason=model_id"); return 5;
            }
            errno = 0;
            const long selected = std::strtol(argv[4], &end, 10);
            if (errno || !end || *end || selected < 0 || selected > INT_MAX ||
                !onion::hyper::model_entry(static_cast<int>(selected))) {
                log("arguments_rejected reason=model_id"); return 5;
            }
            active_model_id = static_cast<int>(selected);
        }
    }
    log("arguments status=ok integrated=%d model=%d", launch_token ? 1 : 0, active_model_id);
    rlimit no_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) != 0) { log("no_core_failed"); return 5; }
    Descriptor lock(open(kLockPath, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (lock.fd < 0 || flock(lock.fd, LOCK_EX | LOCK_NB) != 0) {
        log("worker_lock_unavailable errno=%d", errno);
        (void)notice(hyper_message(HyperMessage::already_running)); return 5;
    }
    const std::uint64_t started = now_ms();
    if (started == UINT64_MAX) { log("monotonic_clock_unavailable"); return 5; }
    watchdog_deadline.store(started + kTurnLimitMs, std::memory_order_release);
    pthread_t receiver{}, supervisor{};
    if (pthread_create(&receiver, nullptr, input_loop, nullptr) != 0) { log("input_thread_failed"); return 5; }
    if (pthread_create(&supervisor, nullptr, supervise, nullptr) != 0) {
        done.store(true, std::memory_order_release); (void)pthread_join(receiver, nullptr);
        log("supervisor_failed"); return 5;
    }
    int result = 5;
    try { result = assistant_loop(); }
    catch (const std::bad_alloc &) { log("worker_no_memory"); (void)notice(hyper_message(HyperMessage::low_memory)); }
    catch (...) { log("worker_initialization_failed"); (void)notice(hyper_message(HyperMessage::interrupted)); }
    done.store(true, std::memory_order_release);
    if (pthread_join(receiver, nullptr) != 0 || pthread_join(supervisor, nullptr) != 0) _exit(5);
    return result;
}
#endif
