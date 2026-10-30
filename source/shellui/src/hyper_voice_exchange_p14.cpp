#include "hyper_voice_exchange_p14.hpp"
#include "hyper_voice_wire.h"
#include "external_symbols.hpp"
#include <onion/log.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr std::uint64_t kExchangeDeadlineMs = 180000;
constexpr std::size_t kIoChunk = 65536;

std::uint64_t monotonic_ms() {
    timespec stamp{};
    if (clock_gettime(CLOCK_MONOTONIC, &stamp) != 0 || stamp.tv_sec < 0 ||
        stamp.tv_nsec < 0 || stamp.tv_nsec >= 1000000000L)
        return UINT64_MAX;
    auto seconds = static_cast<std::uint64_t>(stamp.tv_sec);
    if (seconds > (UINT64_MAX - 999u) / 1000u) return UINT64_MAX;
    return seconds * 1000u + static_cast<std::uint64_t>(stamp.tv_nsec / 1000000L);
}

struct Connection {
    int user;
    bool (*active)(int);
    std::uint64_t started;
    int fd = -1;
    std::uint64_t deadline_ms = kExchangeDeadlineMs;

    ~Connection() { if (fd >= 0) (void)close(fd); }

    int guard() const {
        if (!active(user)) return -ECANCELED;
        const auto now = monotonic_ms();
        if (now == UINT64_MAX || started == UINT64_MAX || now < started) return -EIO;
        return now - started >= deadline_ms ? -ETIMEDOUT : 0;
    }

    int wait(short events) const {
        for (;;) {
            int code = guard();
            if (code < 0) return code;
            pollfd descriptor{fd, events, 0};
            int result = poll(&descriptor, 1, 10);
            if (result < 0) {
                if (errno == EINTR) continue;
                return -(errno ? errno : EIO);
            }
            if ((code = guard()) < 0) return code;
            if (!result) continue;
            if (descriptor.revents & POLLNVAL) return -EBADF;
            if (descriptor.revents & events) return 0;
            if (descriptor.revents & (POLLERR | POLLHUP)) return -ECONNRESET;
        }
    }

    int connect_worker() {
        int code = guard();
        if (code < 0) return code;
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -(errno ? errno : EIO);
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
            return -(errno ? errno : EIO);
        int descriptor_flags = fcntl(fd, F_GETFD, 0);
        if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0)
            return -(errno ? errno : EIO);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        static_assert(sizeof(HYPER_P14_VOICE_SOCKET) <= sizeof(address.sun_path));
        std::memcpy(address.sun_path, HYPER_P14_VOICE_SOCKET, sizeof(HYPER_P14_VOICE_SOCKET));
        if ((code = guard()) < 0) return code;
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), SUN_LEN(&address)) == 0)
            return guard();
        if (errno != EINPROGRESS && errno != EALREADY && errno != EINTR &&
            errno != EAGAIN && errno != EWOULDBLOCK)
            return -(errno ? errno : EIO);
        if ((code = wait(POLLOUT)) < 0) return code;
        int failure = 0;
        socklen_t length = sizeof(failure);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &failure, &length) != 0)
            return -(errno ? errno : EIO);
        if (length != sizeof(failure)) return -EPROTO;
        if (failure < 0) return -EPROTO;
        if (failure) return -failure;
        return guard();
    }

    int send_all(const void *bytes, std::size_t length) const {
        const auto *cursor = static_cast<const unsigned char *>(bytes);
        while (length) {
            int code = guard();
            if (code < 0) return code;
            const auto requested = std::min(length, kIoChunk);
            auto count = send(fd, cursor, requested, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (count < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if ((code = wait(POLLOUT)) < 0) return code;
                    continue;
                }
                return -(errno ? errno : EIO);
            }
            if (count == 0 || static_cast<std::size_t>(count) > requested) return -EPIPE;
            cursor += count;
            length -= static_cast<std::size_t>(count);
        }
        return guard();
    }

    int receive_all(void *bytes, std::size_t length) const {
        auto *cursor = static_cast<unsigned char *>(bytes);
        while (length) {
            int code = guard();
            if (code < 0) return code;
            const auto requested = std::min(length, kIoChunk);
            auto count = recv(fd, cursor, requested, MSG_DONTWAIT);
            if (count < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if ((code = wait(POLLIN)) < 0) return code;
                    continue;
                }
                return -(errno ? errno : EIO);
            }
            if (count == 0 || static_cast<std::size_t>(count) > requested) return -ECONNRESET;
            cursor += count;
            length -= static_cast<std::size_t>(count);
        }
        return guard();
    }

    int send_hello(const hyper_p14_voice_header &hello) const {
        for (;;) {
            int code = guard();
            if (code < 0) return code;
            iovec bytes{const_cast<hyper_p14_voice_header *>(&hello), sizeof(hello)};
            alignas(cmsghdr) unsigned char ancillary[CMSG_SPACE(sizeof(cmsgcred))]{};
            msghdr message{};
            message.msg_iov = &bytes;
            message.msg_iovlen = 1;
            message.msg_control = ancillary;
            message.msg_controllen = sizeof(ancillary);
            cmsghdr *credential = CMSG_FIRSTHDR(&message);
            if (!credential) return -EIO;
            credential->cmsg_level = SOL_SOCKET;
            credential->cmsg_type = SCM_CREDS;
            credential->cmsg_len = CMSG_LEN(sizeof(cmsgcred));
            auto sent = sendmsg(fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if ((code = wait(POLLOUT)) < 0) return code;
                    continue;
                }
                return -(errno ? errno : EIO);
            }
            if (sent <= 0 || static_cast<std::size_t>(sent) > sizeof(hello)) return -EPIPE;
            const auto *cursor = reinterpret_cast<const unsigned char *>(&hello);
            return send_all(cursor + sent, sizeof(hello) - static_cast<std::size_t>(sent));
        }
    }

    bool reject_peer(const char *reason, const msghdr &message, int pid = -1, int lookup = 0) const {
        LOG_WARN("Hyper R33 voice ACK rejected reason=%s flags=0x%08x control_bytes=%zu peer_pid=%d lookup=0x%08x",
                 reason, static_cast<unsigned>(message.msg_flags),
                 static_cast<std::size_t>(message.msg_controllen), pid, static_cast<unsigned>(lookup));
        return false;
    }

    bool trusted(const msghdr &message) const {
        // FreeBSD recvmsg preserves the requested MSG_DONTWAIT bit. EOR is
        // transport record metadata; neither bit substitutes for credentials.
        constexpr unsigned allowed_flags = MSG_DONTWAIT | MSG_EOR;
        if (static_cast<unsigned>(message.msg_flags) & ~allowed_flags)
            return reject_peer("flags", message);
        if (!sceKernelGetProcessName) return reject_peer("resolver_unavailable", message);
        if (!message.msg_control || message.msg_controllen > CMSG_SPACE(sizeof(cmsgcred)))
            return reject_peer("control_length", message);
        unsigned credentials_seen = 0;
        bool matches = false;
        int authenticated_pid = -1;
        const auto *begin = static_cast<const unsigned char *>(message.msg_control);
        const auto *end = begin + message.msg_controllen;
        for (cmsghdr *control = CMSG_FIRSTHDR(const_cast<msghdr *>(&message)); control;
             control = CMSG_NXTHDR(const_cast<msghdr *>(&message), control)) {
            const auto *at = reinterpret_cast<const unsigned char *>(control);
            if (at < begin || at > end || static_cast<std::size_t>(end - at) < sizeof(cmsghdr) ||
                control->cmsg_len < sizeof(cmsghdr) || control->cmsg_len > static_cast<std::size_t>(end - at))
                return reject_peer("malformed_control", message);
            if (control->cmsg_level != SOL_SOCKET || control->cmsg_type != SCM_CREDS) continue;
            if (++credentials_seen != 1 || control->cmsg_len < CMSG_LEN(sizeof(cmsgcred)))
                return reject_peer("credential_length_or_count", message);
            cmsgcred peer{};
            std::memcpy(&peer, CMSG_DATA(control), sizeof(peer));
            if (peer.cmcred_pid <= 1) return reject_peer("invalid_pid", message, peer.cmcred_pid);
            char name[64]{};
            const int lookup = sceKernelGetProcessName(peer.cmcred_pid, name);
            if (lookup < 0) return reject_peer("process_lookup", message, peer.cmcred_pid, lookup);
            name[sizeof(name) - 1] = '\0';
            matches = std::strcmp(name, "SceSpZeroConf") == 0;
            if (!matches) return reject_peer("process_name", message, peer.cmcred_pid, lookup);
            authenticated_pid = peer.cmcred_pid;
        }
        if (credentials_seen != 1 || !matches) return reject_peer("credentials_missing", message);
        LOG_INFO("Hyper R33 voice ACK authenticated flags=0x%08x control_bytes=%zu peer_pid=%d",
                 static_cast<unsigned>(message.msg_flags),
                 static_cast<std::size_t>(message.msg_controllen), authenticated_pid);
        return true;
    }

    int receive_hello(hyper_p14_voice_header &hello) const {
        for (;;) {
            int code = guard();
            if (code < 0) return code;
            iovec bytes{&hello, sizeof(hello)};
            alignas(cmsghdr) unsigned char ancillary[CMSG_SPACE(sizeof(cmsgcred))]{};
            msghdr message{};
            message.msg_iov = &bytes;
            message.msg_iovlen = 1;
            message.msg_control = ancillary;
            message.msg_controllen = sizeof(ancillary);
            auto received = recvmsg(fd, &message, MSG_DONTWAIT);
            if (received < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if ((code = wait(POLLIN)) < 0) return code;
                    continue;
                }
                return -(errno ? errno : EIO);
            }
            if (received == 0 || static_cast<std::size_t>(received) > sizeof(hello)) return -ECONNRESET;
            if (!trusted(message)) return -EACCES;
            auto *cursor = reinterpret_cast<unsigned char *>(&hello);
            return receive_all(cursor + received, sizeof(hello) - static_cast<std::size_t>(received));
        }
    }
};

bool identity(const hyper_p14_voice_header &header, int user, std::uint64_t sequence) {
    return header.magic == HYPER_P14_VOICE_MAGIC && header.version == HYPER_P14_VOICE_VERSION &&
           header.user == user && header.sequence == sequence && header.reserved == 0;
}
} // namespace

int hyper_p14_voice_exchange_session(int user, std::uint64_t sequence, bool (*active)(int),
                                     const std::int16_t *input, std::size_t input_samples,
                                     std::int16_t *output, std::size_t output_capacity,
                                     std::size_t *output_samples, int *session_fd) {
    if (session_fd) *session_fd = -1;
    if (!output_samples) return -EINVAL;
    *output_samples = 0;
    if (!session_fd) return -EINVAL;
    if (user <= 0 || user == 0xfe || user == 0xff || sequence == 0 || !active ||
        !input || !output || input_samples == 0 || input_samples > HYPER_P14_MAX_INPUT_SAMPLES ||
        output_capacity == 0 || output_capacity > std::numeric_limits<std::size_t>::max() / sizeof(std::int16_t))
        return -EINVAL;
    static_assert(HYPER_P14_MAX_INPUT_SAMPLES <= SIZE_MAX / sizeof(std::int16_t));
    static_assert(HYPER_P14_MAX_OUTPUT_SAMPLES <= SIZE_MAX / sizeof(std::int16_t));
    Connection connection{user, active, monotonic_ms()};
    int code = connection.connect_worker();
    if (code < 0) return code;
    hyper_p14_voice_header hello{HYPER_P14_VOICE_MAGIC, HYPER_P14_VOICE_VERSION,
                                HYPER_P14_HELLO, 0, 0, 0, user, 0, sequence};
    if ((code = connection.send_hello(hello)) < 0) return code;
    hyper_p14_voice_header acknowledged{};
    if ((code = connection.receive_hello(acknowledged)) < 0) return code;
    if (!identity(acknowledged, user, sequence) || acknowledged.kind != HYPER_P14_HELLO ||
        acknowledged.sample_rate != 0 || acknowledged.samples != 0 || acknowledged.status != 0)
        return -EPROTO;
    hyper_p14_voice_header capture{HYPER_P14_VOICE_MAGIC, HYPER_P14_VOICE_VERSION,
                                  HYPER_P14_CAPTURE, 16000, static_cast<std::uint32_t>(input_samples),
                                  0, user, 0, sequence};
    if ((code = connection.send_all(&capture, sizeof(capture))) < 0) return code;
    if ((code = connection.send_all(input, input_samples * sizeof(*input))) < 0) return code;
    hyper_p14_voice_header reply{};
    if ((code = connection.receive_all(&reply, sizeof(reply))) < 0) return code;
    if (!identity(reply, user, sequence) || reply.kind != HYPER_P14_REPLY) return -EPROTO;
    if (reply.status < 0) {
        if (reply.samples != 0 || (reply.sample_rate != 0 && reply.sample_rate != 48000)) return -EPROTO;
        return reply.status;
    }
    if (reply.status != 0 || reply.sample_rate != 48000 || reply.samples == 0 ||
        reply.samples > HYPER_P14_MAX_OUTPUT_SAMPLES || reply.samples > output_capacity)
        return -EPROTO;
    if ((code = connection.receive_all(output, static_cast<std::size_t>(reply.samples) * sizeof(*output))) < 0)
        return code;
    if ((code = connection.guard()) < 0) return code;
    *output_samples = reply.samples;
    *session_fd = connection.fd;
    connection.fd = -1;
    return 0;
}

int hyper_p14_voice_exchange(int user, std::uint64_t sequence, bool (*active)(int),
                             const std::int16_t *input, std::size_t input_samples,
                             std::int16_t *output, std::size_t output_capacity,
                             std::size_t *output_samples) {
    int session_fd = -1;
    const int result = hyper_p14_voice_exchange_session(user, sequence, active, input,
                                                       input_samples, output, output_capacity,
                                                       output_samples, &session_fd);
    if (session_fd >= 0) (void)close(session_fd);
    return result;
}

int hyper_p14_voice_finish(int *session_fd, int user, std::uint64_t sequence,
                           bool (*active)(int), int status, std::size_t samples) {
    if (!session_fd) return -EINVAL;
    const int owned_fd = *session_fd;
    *session_fd = -1;
    if (owned_fd < 0) return 0;
    Connection connection{user, active, monotonic_ms()};
    connection.fd = owned_fd;
    connection.deadline_ms = 2000;
    if (!active || user <= 0 || user == 0xfe || user == 0xff || sequence == 0 || status > 0 ||
        (status == 0 && (samples == 0 || samples > HYPER_P14_MAX_OUTPUT_SAMPLES)) ||
        (status < 0 && samples != 0)) return -EINVAL;
    hyper_p14_voice_header completed{HYPER_P14_VOICE_MAGIC, HYPER_P14_VOICE_VERSION,
                                     HYPER_P14_DONE, 48000, static_cast<std::uint32_t>(samples),
                                     status, user, 0, sequence};
    return connection.send_all(&completed, sizeof(completed));
}
