#define _DEFAULT_SOURCE 1
#include "native_input.h"
#include "native_input_wire.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef HYPER_P5_PS5
#include <ps5/klog.h>
#endif

/* All state belongs to this standalone worker, never to ShellUI. */
static int input_socket = -1;
static bool socket_owned;
static void *user_module;
static void *kernel_module;
static int (*initialize_users)(const void *);
static int (*terminate_users)(void);
static int (*foreground_user)(int *);
static int (*process_name)(int, char *);
static bool users_initialized;
static struct hyper_p5_sample last_sample;
static bool have_sample;
static bool sender_rejection_logged;
static bool order_warning_logged;

static bool bind_api(void *module, const char *name, void *destination,
                     size_t size) {
    void *symbol = dlsym(module, name);
    if (!symbol || size != sizeof(symbol)) return false;
    memcpy(destination, &symbol, size);
    return true;
}

static void log_stage(const char *stage, int code) {
#ifdef HYPER_P5_PS5
    (void)klog_printf("[Hyper P13 input] %s: %d\n", stage, code);
#else
    (void)stage;
    (void)code;
#endif
}

static int init_provider(void) {
    if (input_socket >= 0) return 0;
    user_module = dlopen("libSceUserService.sprx", RTLD_LAZY | RTLD_LOCAL);
    kernel_module = dlopen("libkernel_sys.sprx", RTLD_LAZY | RTLD_LOCAL);
    if (!user_module || !kernel_module ||
        !bind_api(user_module, "sceUserServiceInitialize",
                  &initialize_users, sizeof(initialize_users)) ||
        !bind_api(user_module, "sceUserServiceTerminate",
                  &terminate_users, sizeof(terminate_users)) ||
        !bind_api(user_module, "sceUserServiceGetForegroundUser",
                  &foreground_user, sizeof(foreground_user)) ||
        !bind_api(kernel_module, "sceKernelGetProcessName",
                  &process_name, sizeof(process_name)))
        return -ENOSYS;

    /* The working HyperHEN audio path initializes UserService in each
     * payload process before asking for the foreground user. */
    const int priority = 256;
    int user_result = initialize_users(&priority);
    users_initialized = user_result >= 0;
    if (!users_initialized) log_stage("user-service init", user_result);

    input_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (input_socket < 0) {
        int failure = errno;
        log_stage("socket", failure);
        return -failure;
    }
    (void)fcntl(input_socket, F_SETFD, FD_CLOEXEC);
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, HYPER_P5_INPUT_SOCKET,
           sizeof(HYPER_P5_INPUT_SOCKET));
    /* main() already owns the audio-probe lock: only our exact stale socket
     * can be cleared; any regular file or foreign owner is rejected. */
    struct stat existing;
    if (lstat(HYPER_P5_INPUT_SOCKET, &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || existing.st_uid != geteuid() ||
            unlink(HYPER_P5_INPUT_SOCKET) != 0) {
            (void)close(input_socket);
            input_socket = -1;
            log_stage("stale socket", EEXIST);
            return -EEXIST;
        }
    } else if (errno != ENOENT) {
        int failure = errno;
        (void)close(input_socket);
        input_socket = -1;
        log_stage("socket lstat", failure);
        return -failure;
    }
    mode_t previous_umask = umask(0077);
    /* Match HyperHEN's working AF_UNIX IPC path: FreeBSD expects SUN_LEN,
     * not the full zero-padded sockaddr_un storage. */
    int code = bind(input_socket, (struct sockaddr *)&address, SUN_LEN(&address));
    int failure = errno;
    (void)umask(previous_umask);
    if (code != 0) {
        (void)close(input_socket);
        input_socket = -1;
        log_stage("socket bind", failure);
        return -failure;
    }
    /* ShellUI may have a different UID than the worker; SCM_CREDS still
     * authenticates each datagram before it can request synthetic tones. */
    if (chmod(HYPER_P5_INPUT_SOCKET, 0622) != 0) {
        failure = errno;
        (void)close(input_socket);
        input_socket = -1;
        (void)unlink(HYPER_P5_INPUT_SOCKET);
        log_stage("socket chmod", failure);
        return -failure;
    }
    socket_owned = true;
    return 0;
}

static bool current_user(int *user) {
    int result = foreground_user(user);
    if (result < 0) {
        log_stage("foreground user", result);
        return false;
    }
    if (!hyper_p5_user_valid(*user)) {
        log_stage("invalid foreground user", *user);
        return false;
    }
    return true;
}

static bool reject_sender(const char *stage, int code) {
    if (!sender_rejection_logged) {
        sender_rejection_logged = true;
        log_stage(stage, code);
    }
    return false;
}

static bool trusted_sender(const struct msghdr *message) {
    if (message->msg_flags & MSG_CTRUNC)
        return reject_sender("sender credentials truncated", 0);
    for (struct cmsghdr *control = CMSG_FIRSTHDR(message); control;
         control = CMSG_NXTHDR(message, control)) {
        if (control->cmsg_level != SOL_SOCKET ||
            control->cmsg_type != SCM_CREDS ||
            control->cmsg_len < CMSG_LEN(sizeof(struct cmsgcred)))
            continue;
        struct cmsgcred credentials;
        memcpy(&credentials, CMSG_DATA(control), sizeof(credentials));
        if (credentials.cmcred_pid <= 1)
            return reject_sender("sender PID invalid", credentials.cmcred_pid);
        char name[64] = {0};
        int result = process_name(credentials.cmcred_pid, name);
        if (result < 0)
            return reject_sender("sender process name", result);
        name[sizeof(name) - 1] = '\0';
        if (strcmp(name, "SceShellUI") != 0)
            return reject_sender("sender is not ShellUI", 0);
        return true;
    }
    return reject_sender("sender credentials absent", 0);
}

int hyper_p5_native_read_input(struct hyper_p5_sample *sample) {
    if (!sample) return -EINVAL;
    int code = init_provider();
    if (code < 0) return code;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        struct hyper_p5_input_frame frame;
        memset(&frame, 0, sizeof(frame));
        struct iovec data = {&frame, sizeof(frame)};
        union {
            struct cmsghdr align;
            unsigned char buffer[CMSG_SPACE(sizeof(struct cmsgcred))];
        } ancillary = {0};
        struct msghdr message;
        memset(&message, 0, sizeof(message));
        message.msg_iov = &data;
        message.msg_iovlen = 1;
        message.msg_control = ancillary.buffer;
        message.msg_controllen = sizeof(ancillary.buffer);
        ssize_t received = recvmsg(input_socket, &message, MSG_DONTWAIT);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            int failure = errno;
            log_stage("socket recvmsg", failure);
            return -failure;
        }
        if (received != (ssize_t)sizeof(frame) ||
            frame.magic != HYPER_P5_INPUT_MAGIC ||
            frame.version != HYPER_P5_INPUT_VERSION ||
            frame.device_index != 0 || frame.sequence == 0 ||
            !trusted_sender(&message))
            continue;
        /* GetData can run on concurrent ShellUI threads. Sequence allocation
         * precedes sendmsg, so delivery may be reordered. Never let an older
         * datagram overwrite the last authenticated controller snapshot. */
        if (have_sample && frame.sequence <= last_sample.sequence) {
            if (!order_warning_logged) {
                order_warning_logged = true;
                log_stage("out-of-order input ignored", 0);
            }
            continue;
        }
        int user = -1;
        if (!current_user(&user)) return -EACCES;
        last_sample.sequence = frame.sequence;
        last_sample.user = user;
        last_sample.buttons = frame.buttons;
        last_sample.connected = true;
        have_sample = true;
    }
    if (!have_sample) return -EAGAIN;
    *sample = last_sample;
    return 0;
}

__attribute__((destructor))
static void cleanup_provider(void) {
    if (input_socket >= 0) (void)close(input_socket);
    if (socket_owned) (void)unlink(HYPER_P5_INPUT_SOCKET);
    if (users_initialized && terminate_users) (void)terminate_users();
    if (user_module) (void)dlclose(user_module);
    if (kernel_module) (void)dlclose(kernel_module);
}
