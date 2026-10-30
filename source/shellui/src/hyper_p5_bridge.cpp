#include "hyper_p5_bridge.hpp"
#include "native_input_wire.h"
#include "startup_audio.h"
#include "external_symbols.hpp"
#include "hyper_mic_monitor.hpp"
#include "hyper_voice_consent.hpp"
#include "hyper_audio_echo.hpp"
#include "hyper_output_route_probe.hpp"
#include "hyper_output_tone.hpp"
#include "hyper_voice_echo_p13.hpp"
#include "hyper_voice_assistant_p14.hpp"
#include "hyper_voice_wire.h"
#include "hyper_input_mailbox.hpp"
#include "hyper_voice_session_policy.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <onion/log.h>
#include <onion/hyper_lifecycle.hpp>

namespace {
std::atomic<int> socket_fd{-1};
std::atomic<bool> reported_error{false};
std::atomic<bool> audio_listener_started{false};
std::atomic<bool> input_worker_started{false};
std::atomic<bool> input_worker_active{false};
std::atomic<bool> assistant_enabled{true};
HyperInputMailbox input_mailbox;
std::atomic<bool> mic_probe_attempted{false};
pthread_mutex_t input_state_mutex = PTHREAD_MUTEX_INITIALIZER;
uint64_t observed_sequence = 0;
uint64_t observed_ms = 0;
uint32_t observed_buttons = 0x80000000u;
uint64_t monitor_start_generation = 0;
bool monitor_latch_active = false;
std::atomic<bool> session_guard_reported{false};
HyperVoiceConsent voice_consent;
uint64_t consent_observed_generation = 0;
uint64_t voice_consent_generation = 0;

extern "C" int sceUserServiceGetForegroundUser(int *user_id);

uint64_t monotonic_ms() {
  timespec timestamp{};
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0 || timestamp.tv_sec < 0)
    return UINT64_MAX;
  return static_cast<uint64_t>(timestamp.tv_sec) * 1000u +
         static_cast<uint64_t>(timestamp.tv_nsec) / 1000000u;
}

void observe_input_state(const hyper_p5_input_frame &frame) {
  const uint64_t now = monotonic_ms();
  if (now == UINT64_MAX || pthread_mutex_trylock(&input_state_mutex) != 0)
    return;
  if (frame.sequence > observed_sequence) {
    const uint64_t generation = input_mailbox.cancellation();
    if (generation != consent_observed_generation) {
      voice_consent.cancel();
      consent_observed_generation = generation;
    }
    observed_sequence = frame.sequence;
    observed_ms = now;
    observed_buttons = frame.buttons;
    if (voice_consent.observe(frame.sequence, now, frame.buttons)) {
      int user = -1;
      if (sceUserServiceGetForegroundUser(&user) < 0) user = -1;
      voice_consent.bind_user(user);
      voice_consent_generation = generation;
    }
  }
  (void)pthread_mutex_unlock(&input_state_mutex);
}

void report_session_guard(HyperVoiceGuardReason reason, uint64_t age,
                          uint64_t generation, uint64_t expected, bool processing) {
  if (reason == HyperVoiceGuardReason::active ||
      session_guard_reported.exchange(true, std::memory_order_acq_rel)) return;
  LOG_INFO("Hyper R44 session_cancel reason=%u age_ms=%llu generation=%llu expected=%llu processing=%u",
      static_cast<unsigned>(reason), static_cast<unsigned long long>(age),
      static_cast<unsigned long long>(generation), static_cast<unsigned long long>(expected),
      processing ? 1u : 0u);
}

bool monitor_session_valid(int user, uint64_t max_age, bool report) {
  auto reject = [&](HyperVoiceGuardReason reason) {
    if (report) report_session_guard(reason, UINT64_MAX, 0, 0,
                                     max_age == kHyperVoiceProcessingFreshMs);
    return false;
  };
  if (!assistant_enabled.load(std::memory_order_acquire) ||
      access(onion::hyper::kDisabledMarker, F_OK) == 0)
    return reject(HyperVoiceGuardReason::disabled);
  if (!input_worker_active.load(std::memory_order_acquire))
    return reject(HyperVoiceGuardReason::provider);
  int foreground = -1;
  if (sceUserServiceGetForegroundUser(&foreground) < 0 || foreground != user)
    return reject(HyperVoiceGuardReason::foreground);
  if (pthread_mutex_lock(&input_state_mutex) != 0)
    return reject(HyperVoiceGuardReason::clock_or_lock);
  // Read time after acquiring the snapshot lock: an observer may publish a
  // newer timestamp while this thread waits for the mutex.
  const uint64_t now = monotonic_ms();
  if (now == UINT64_MAX) {
    (void)pthread_mutex_unlock(&input_state_mutex);
    return reject(HyperVoiceGuardReason::clock_or_lock);
  }
  const HyperVoiceSessionInput input{now, observed_ms, observed_sequence,
      observed_buttons, input_mailbox.cancellation(), consent_observed_generation,
      monitor_start_generation, monitor_latch_active};
  const auto reason = hyper_voice_session_reason(input, max_age);
  (void)pthread_mutex_unlock(&input_state_mutex);
  if (report && reason != HyperVoiceGuardReason::active)
    report_session_guard(reason, now >= input.observed_ms ? now - input.observed_ms : UINT64_MAX,
        input.cancellation, input.start_generation, max_age == kHyperVoiceProcessingFreshMs);
  return reason == HyperVoiceGuardReason::active;
}

bool monitor_session_active(int user) {
  return monitor_session_valid(user, kHyperVoiceInputFreshMs, true);
}
bool monitor_processing_active(int user) {
  return monitor_session_valid(user, kHyperVoiceProcessingFreshMs, true);
}
bool monitor_fresh_input(int user) {
  return monitor_session_valid(user, kHyperVoiceInputFreshMs, false);
}
bool monitor_notice_allowed(int user) {
  int foreground = -1;
  if (sceUserServiceGetForegroundUser(&foreground) < 0) return false;
  return hyper_voice_terminal_notice_allowed(assistant_enabled.load(std::memory_order_acquire),
      access(onion::hyper::kDisabledMarker, F_OK) == 0, user, foreground);
}

bool monitor_request_is_fresh(uint64_t sequence) {
  if (pthread_mutex_lock(&input_state_mutex) != 0) return false;
  const bool fresh = sequence <= observed_sequence &&
                     observed_sequence - sequence <= 128u;
  (void)pthread_mutex_unlock(&input_state_mutex);
  return fresh;
}

bool begin_monitor_session() {
  if (pthread_mutex_lock(&input_state_mutex) != 0) return false;
  monitor_start_generation = input_mailbox.cancellation();
  monitor_latch_active = true;
  session_guard_reported.store(false, std::memory_order_release);
  (void)pthread_mutex_unlock(&input_state_mutex);
  return true;
}

bool begin_voice_session(int user, uint64_t sequence) {
  if (pthread_mutex_lock(&input_state_mutex) != 0) return false;
  const uint64_t now = monotonic_ms();
  if (now == UINT64_MAX) {
    (void)pthread_mutex_unlock(&input_state_mutex);
    return false;
  }
  const uint64_t generation = input_mailbox.cancellation();
  const bool gesture = voice_consent.consume(user, sequence, now);
  const bool allowed = gesture && generation == voice_consent_generation;
  if (allowed) {
    monitor_start_generation = generation;
    monitor_latch_active = true;
    session_guard_reported.store(false, std::memory_order_release);
  }
  (void)pthread_mutex_unlock(&input_state_mutex);
  return allowed;
}

void end_monitor_session() {
  if (pthread_mutex_lock(&input_state_mutex) != 0) return;
  monitor_latch_active = false;
  (void)pthread_mutex_unlock(&input_state_mutex);
}

bool send_mic_notice(const char *message) {
  if (!sceKernelSendNotificationRequest) return false;
  OrbisNotificationRequest request{};
  (void)std::snprintf(request.message, sizeof(request.message),
                      "[Hyper] %s", message);
  return sceKernelSendNotificationRequest(0, &request, sizeof(request), 0) >= 0;
}

void probe_shellui_input(int user) {
  if (!send_mic_notice("Verifica ingresso audio. Nessuna voce registrata.")) {
    LOG_WARN("Hyper P8 ShellUI MIC notice failed; input not opened");
    return;
  }
  void *module = dlopen("libSceAudioIn.sprx", RTLD_LAZY | RTLD_LOCAL);
  if (!module) {
    LOG_WARN("Hyper P8 ShellUI MIC AudioIn module unavailable");
    (void)send_mic_notice("Ingresso audio non disponibile.");
    return;
  }
  using open_fn = int (*)(int, int, int, uint32_t, uint32_t, uint32_t);
  using close_fn = int (*)(int);
  static_assert(sizeof(open_fn) == sizeof(void *));
  static_assert(sizeof(close_fn) == sizeof(void *));
  open_fn open_port = nullptr;
  close_fn close_port = nullptr;
  void *symbol = dlsym(module, "sceAudioInOpen");
  if (symbol) std::memcpy(&open_port, &symbol, sizeof(open_port));
  symbol = dlsym(module, "sceAudioInClose");
  if (symbol) std::memcpy(&close_port, &symbol, sizeof(close_port));
  if (!open_port || !close_port) {
    LOG_WARN("Hyper P8 ShellUI MIC AudioIn symbols unavailable");
    (void)dlclose(module);
    (void)send_mic_notice("Ingresso audio non disponibile.");
    return;
  }
  /* Probe only: the selected physical microphone remains unverified.
   * No call to sceAudioInInput and no PCM buffer is ever made here. */
  const int handle = open_port(user, 1, 0, 256, 16000, 1);
  LOG_INFO("Hyper P8 ShellUI MIC open code=0x%08x", (unsigned)handle);
  if (handle < 0) {
    (void)dlclose(module);
    (void)send_mic_notice("Apertura microfono non riuscita.");
    return;
  }
  const int closed = close_port(handle);
  LOG_INFO("Hyper P8 ShellUI MIC close code=0x%08x", (unsigned)closed);
  (void)dlclose(module);
  if (closed < 0) {
    (void)send_mic_notice("Chiusura ingresso audio non confermata. Riavvia la console.");
    return;
  }
  LOG_INFO("Hyper P8 ShellUI MIC probe completed; no PCM read");
  (void)send_mic_notice("Ingresso audio disponibile. Nessuna registrazione.");
}

bool audio_sender_is_trusted(const msghdr &message,
                             const hyper_p5_audio_command &command) {
  if (!assistant_enabled.load(std::memory_order_acquire) ||
      !input_worker_active.load(std::memory_order_acquire) ||
      message.msg_flags & (MSG_CTRUNC | MSG_TRUNC) || !sceKernelGetProcessName ||
      command.magic != HYPER_P5_AUDIO_MAGIC ||
      command.version != HYPER_P5_AUDIO_VERSION ||
      (command.action != HYPER_P5_AUDIO_PLAY_STARTUP &&
       command.action != HYPER_P5_AUDIO_PROBE_INPUT &&
       command.action != HYPER_P5_AUDIO_MONITOR_MUTE &&
       command.action != HYPER_P5_AUDIO_CAPTURE_ECHO &&
       command.action != HYPER_P5_AUDIO_PROBE_ROUTE &&
       command.action != HYPER_P5_AUDIO_PLAY_TONES &&
       command.action != HYPER_P13_AUDIO_CAPTURE_ECHO &&
       command.action != HYPER_P14_AUDIO_REQUEST) ||
      command.user <= 0 || command.sequence == 0)
    return false;
  int foreground = -1;
  if (sceUserServiceGetForegroundUser(&foreground) < 0 ||
      foreground != command.user)
    return false;
  for (cmsghdr *control = CMSG_FIRSTHDR(&message); control;
       control = CMSG_NXTHDR(&message, control)) {
    if (control->cmsg_level != SOL_SOCKET ||
        control->cmsg_type != SCM_CREDS ||
        control->cmsg_len < CMSG_LEN(sizeof(cmsgcred)))
      continue;
    cmsgcred credentials{};
    std::memcpy(&credentials, CMSG_DATA(control), sizeof(credentials));
    if (credentials.cmcred_pid <= 1) return false;
    char name[64]{};
    if (sceKernelGetProcessName(credentials.cmcred_pid, name) < 0)
      return false;
    name[sizeof(name) - 1] = '\0';
    return std::strcmp(name, "SceSpZeroConf") == 0;
  }
  return false;
}

void *audio_listener(void *) {
  struct ListenerExit {
    ~ListenerExit() { audio_listener_started.store(false, std::memory_order_release); }
  } listener_exit;
  int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) {
    LOG_WARN("Hyper P5 ShellUI speaker socket failed: errno=%d", errno);
    return nullptr;
  }
  (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
    (void)close(fd);
    return nullptr;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, HYPER_P5_AUDIO_SOCKET,
              sizeof(HYPER_P5_AUDIO_SOCKET));
  struct stat existing{};
  if (lstat(HYPER_P5_AUDIO_SOCKET, &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode) || existing.st_uid != geteuid() ||
        unlink(HYPER_P5_AUDIO_SOCKET) != 0) {
      LOG_WARN("Hyper P5 ShellUI speaker socket path unavailable");
      (void)close(fd);
      return nullptr;
    }
  } else if (errno != ENOENT) {
    LOG_WARN("Hyper P5 ShellUI speaker socket stat failed: errno=%d", errno);
    (void)close(fd);
    return nullptr;
  }
  int bound = bind(fd, reinterpret_cast<sockaddr *>(&address),
                   SUN_LEN(&address));
  int failure = errno;
  if (bound != 0) {
    LOG_WARN("Hyper P5 ShellUI speaker bind failed: errno=%d", failure);
    (void)close(fd);
    return nullptr;
  }
  if (chmod(HYPER_P5_AUDIO_SOCKET, 0622) != 0) {
    LOG_WARN("Hyper P5 ShellUI speaker chmod failed: errno=%d", errno);
    (void)close(fd);
    (void)unlink(HYPER_P5_AUDIO_SOCKET);
    return nullptr;
  }
  LOG_INFO("Hyper P14 ShellUI audio listener ready (local assistant bridge)");
  while (input_worker_active.load(std::memory_order_acquire)) {
    hyper_p5_audio_command command{};
    iovec payload{&command, sizeof(command)};
    alignas(cmsghdr) unsigned char control[CMSG_SPACE(sizeof(cmsgcred))]{};
    msghdr message{};
    message.msg_iov = &payload;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t received = recvmsg(fd, &message, 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(10000);
        continue;
      }
      LOG_WARN("Hyper P5 ShellUI speaker receive failed: errno=%d", errno);
      break;
    }
    if (received != static_cast<ssize_t>(sizeof(command)) ||
        !audio_sender_is_trusted(message, command))
      continue;
    if (command.action == HYPER_P5_AUDIO_PLAY_STARTUP) {
      LOG_INFO("Hyper P8 ShellUI speaker request accepted");
      if (shellui_play_startup_voice())
        LOG_INFO("Hyper P8 ShellUI speaker playback completed");
      else
        LOG_WARN("Hyper P8 ShellUI speaker playback failed");
      continue;
    }
    if (command.action == HYPER_P5_AUDIO_MONITOR_MUTE &&
        (!monitor_request_is_fresh(command.sequence) ||
         !monitor_session_active(command.user))) {
      LOG_WARN("Hyper P9 ShellUI mute request rejected: input stale or cancelled");
      continue;
    }
    const bool voice_request = command.action == HYPER_P5_AUDIO_CAPTURE_ECHO;
    const bool route_request = command.action == HYPER_P5_AUDIO_PROBE_ROUTE;
    const bool tone_request = command.action == HYPER_P5_AUDIO_PLAY_TONES;
    const bool p13_request = command.action == HYPER_P13_AUDIO_CAPTURE_ECHO;
    const bool assistant_request = command.action == HYPER_P14_AUDIO_REQUEST;
    const bool local_gesture_request = voice_request || route_request || tone_request || p13_request || assistant_request;
    if (local_gesture_request && !begin_voice_session(command.user, command.sequence)) {
      LOG_WARN("Hyper P12 ShellUI request rejected: no fresh local combo consent");
      continue;
    }
    bool expected = false;
    if (!assistant_request && !mic_probe_attempted.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel)) {
      if (local_gesture_request) end_monitor_session();
      LOG_WARN("Hyper P8 ShellUI MIC repeated request ignored");
      continue;
    }
    if (assistant_request) {
      LOG_INFO("Hyper P14 assistant request accepted with fresh local combo");
      hyper_p14_voice_assistant(command.user, command.sequence, monitor_session_active, send_mic_notice,
          monitor_processing_active, monitor_fresh_input, monitor_notice_allowed);
      end_monitor_session();
    } else if (p13_request) {
      LOG_INFO("Hyper P13 ShellUI voice request accepted with local combo consent");
      hyper_p13_voice_echo(command.user, monitor_session_active, send_mic_notice);
      end_monitor_session();
    } else if (tone_request) {
      LOG_INFO("Hyper P12 ShellUI tone request accepted with local combo consent");
      hyper_p12_output_tone(command.user, monitor_session_active, send_mic_notice);
      end_monitor_session();
    } else if (route_request) {
      LOG_INFO("Hyper P11 ShellUI route probe accepted with local combo consent");
      hyper_p11_probe_output_route(command.user, monitor_session_active, send_mic_notice);
      end_monitor_session();
    } else if (voice_request) {
      LOG_INFO("Hyper P10 ShellUI voice request accepted with local combo consent");
      hyper_p10_voice_echo(command.user, monitor_session_active, send_mic_notice);
      end_monitor_session();
    } else if (command.action == HYPER_P5_AUDIO_MONITOR_MUTE) {
      if (!begin_monitor_session()) {
        LOG_WARN("Hyper P9 ShellUI mute monitor state unavailable");
        continue;
      }
      LOG_INFO("Hyper P9 ShellUI mute monitor request accepted");
      hyper_p9_monitor_mute(command.user, monitor_session_active, send_mic_notice);
      end_monitor_session();
    } else {
      LOG_INFO("Hyper P8 ShellUI MIC request accepted");
      probe_shellui_input(command.user);
    }
  }
  (void)close(fd);
  (void)unlink(HYPER_P5_AUDIO_SOCKET);
  return nullptr;
}

void ensure_audio_listener() {
  bool expected = false;
  if (!audio_listener_started.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel))
    return;
  pthread_t thread{};
  int result = pthread_create(&thread, nullptr, audio_listener, nullptr);
  if (result != 0) {
    audio_listener_started.store(false, std::memory_order_release);
    LOG_WARN("Hyper P5 ShellUI speaker listener unavailable: code=%d", result);
    return;
  }
  (void)pthread_detach(thread);
}

int get_socket() {
  int fd = socket_fd.load(std::memory_order_acquire);
  if (fd >= 0) return fd;
  int candidate = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (candidate < 0) return -1;
  (void)fcntl(candidate, F_SETFD, FD_CLOEXEC);
  int expected = -1;
  if (socket_fd.compare_exchange_strong(expected, candidate,
                                        std::memory_order_acq_rel))
    return candidate;
  (void)close(candidate);
  return expected;
}

uint32_t native_buttons(uint32_t managed) {
  constexpr uint32_t kManagedSquare = 16u;
  constexpr uint32_t kManagedCircle = 64u;
  constexpr uint32_t kL1 = 1024u;
  constexpr uint32_t kR1 = 2048u;
  constexpr uint32_t kIntercepted = 0x80000000u;
  constexpr uint32_t kKnown = kManagedSquare | kManagedCircle | kL1 | kR1 |
                              kIntercepted;
  uint32_t buttons = managed & (kL1 | kR1 | kIntercepted);
  if (managed & kManagedSquare) buttons |= 0x8000u;
  if (managed & kManagedCircle) buttons |= 0x2000u;
  if (managed & ~kKnown) buttons |= 1u;
  return buttons;
}
void send_input_frame(const hyper_p5_input_frame &frame) {
  const int fd = get_socket();
  if (fd < 0) return;

  sockaddr_un destination{};
  destination.sun_family = AF_UNIX;
  std::memcpy(destination.sun_path, HYPER_P5_INPUT_SOCKET,
              sizeof(HYPER_P5_INPUT_SOCKET));
  iovec payload{const_cast<hyper_p5_input_frame *>(&frame), sizeof(frame)};

  /* FreeBSD fills the all-zero credential record itself. The receiver checks
   * the kernel-provided sender identity before a frame can arm the mic. */
  alignas(cmsghdr) unsigned char control[CMSG_SPACE(sizeof(cmsgcred))]{};
  msghdr message{};
  message.msg_name = &destination;
  message.msg_namelen = SUN_LEN(&destination);
  message.msg_iov = &payload;
  message.msg_iovlen = 1;
  message.msg_control = control;
  message.msg_controllen = sizeof(control);
  cmsghdr *credential = CMSG_FIRSTHDR(&message);
  if (!credential) return;
  credential->cmsg_len = CMSG_LEN(sizeof(cmsgcred));
  credential->cmsg_level = SOL_SOCKET;
  credential->cmsg_type = SCM_CREDS;
  const int sent = static_cast<int>(sendmsg(fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL));
  if (sent == static_cast<int>(sizeof(frame))) {
    reported_error.store(false, std::memory_order_relaxed);
  } else if (errno != ENOENT && errno != EAGAIN && errno != EWOULDBLOCK &&
             !reported_error.exchange(true, std::memory_order_relaxed)) {
    LOG_WARN("Hyper P5 input transport failed: errno=%d", errno);
  }
}

bool worker_socket_present() {
  struct stat info{};
  /* Presence is an activation hint, never sender authentication. Commands
   * continue to require kernel SCM_CREDS and a fresh local physical gesture. */
  return lstat(HYPER_P5_INPUT_SOCKET, &info) == 0 && S_ISSOCK(info.st_mode);
}

void cancel_input_session() {
  input_mailbox.cancel();
  if (pthread_mutex_lock(&input_state_mutex) != 0) return;
  voice_consent.cancel();
  observed_sequence = observed_ms = 0;
  observed_buttons = HyperInputMailbox::Cancel;
  /* Keep an in-flight audio session latched to its original cancellation
   * epoch until that session exits. A later release must not re-enable it. */
  (void)pthread_mutex_unlock(&input_state_mutex);
}

void *input_worker(void *) {
  HyperInputCursor cursor;
  bool active = false;
  unsigned presence_polls = 0;
  for (;;) {
    const bool allowed = assistant_enabled.load(std::memory_order_acquire);
    if (!allowed || !active || presence_polls++ >= 125) {
      presence_polls = 0;
      const bool present = allowed && worker_socket_present();
      if (present != active) {
        input_worker_active.store(false, std::memory_order_release);
        cancel_input_session();
        cursor.discard(input_mailbox);
        active = present;
        input_worker_active.store(active, std::memory_order_release);
        if (!active) {
          const int fd = socket_fd.exchange(-1, std::memory_order_acq_rel);
          if (fd >= 0) (void)close(fd);
        }
      }
      if (active) ensure_audio_listener();
    }
    if (!active) {
      cursor.discard(input_mailbox);
      usleep(250000);
      continue;
    }
    HyperInputCursor::Sample sample{};
    if (cursor.take(input_mailbox, sample)) {
      if (sample.gap) {
        cancel_input_session();
        cursor.acknowledge_cancellation(input_mailbox);
        sample.buttons |= HyperInputMailbox::Cancel;
      }
      hyper_p5_input_frame frame{};
      frame.magic = HYPER_P5_INPUT_MAGIC;
      frame.version = HYPER_P5_INPUT_VERSION;
      frame.sequence = sample.sequence;
      frame.buttons = sample.buttons;
      frame.device_index = 0;
      observe_input_state(frame);
      send_input_frame(frame);
    }
    usleep(2000);
  }
}
} // namespace

void hyper_p5_bridge_set_enabled(bool value) {
  assistant_enabled.store(value, std::memory_order_release);
  if (!value) cancel_input_session();
}

void hyper_p5_bridge_observe(int device_index, bool skipped, uint32_t buttons) {
  if (device_index != 0) return;
  input_mailbox.publish(native_buttons(buttons) | (skipped ? 0x80000000u : 0u));
}

void hyper_p5_bridge_start() {
  bool expected = false;
  if (!input_worker_started.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel))
    return;
  pthread_t thread{};
  const int result = pthread_create(&thread, nullptr, input_worker, nullptr);
  if (result != 0) {
    input_worker_started.store(false, std::memory_order_release);
    LOG_WARN("Hyper R29 input worker unavailable: code=%d", result);
    return;
  }
  (void)pthread_detach(thread);
  LOG_INFO("Hyper R29 passive input worker started; dormant until standalone worker is available");
}
