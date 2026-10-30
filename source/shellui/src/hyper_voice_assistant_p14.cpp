#include "hyper_voice_assistant_p14.hpp"
#include "hyper_voice_exchange_p14.hpp"
#include "hyper_voice_wire.h"
#include "hyper_locale.hpp"
#include "hyper_audio_module.hpp"
#include "hyper_voice_session_policy.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <time.h>

#include <onion/log.h>

namespace {
constexpr size_t kGrain = 256;
constexpr size_t kInputBlocks = 750;
constexpr size_t kInputSamples = kInputBlocks * kGrain;
constexpr size_t kOutputBlocks = 7500;
constexpr size_t kPage = 16384;
constexpr size_t kInputBytes = (kInputSamples * sizeof(int16_t) + kPage - 1) /
                             kPage * kPage;
constexpr size_t kMaximumContextBytes = 4u * 1024u * 1024u;
constexpr uint64_t kInvalidHandle = UINT64_MAX;

// ABI and port flags are the same local AudioOut2 bindings validated by P12.
struct ContextParam {
  uint32_t max_ports, max_object_ports, guarantee_object_ports;
  uint32_t queue_depth, num_grains, flags, reserved[10];
};
struct PortParam {
  uint16_t port_type, pad;
  uint32_t data_format, sampling_frequency, flags;
  uintptr_t user_handle;
  uint32_t reserved[10];
};
struct PortState {
  uint16_t outputs;
  uint8_t channels, pad1;
  int16_t volume;
  uint16_t reroute_counter;
  uint32_t flags, pad2;
  uint64_t reserved[6];
};
struct Attribute { uint32_t id, pad; void *value; size_t value_size; };
struct Pcm { void *data; };
static_assert(sizeof(ContextParam) == 64 && offsetof(ContextParam, flags) == 20);
static_assert(sizeof(PortParam) == 64 && offsetof(PortParam, user_handle) == 16);
static_assert(sizeof(PortState) == 64 && offsetof(PortState, channels) == 2);
static_assert(sizeof(Attribute) == 24 && offsetof(Attribute, value) == 8);
static_assert(sizeof(Pcm) == 8 && sizeof(float) == 4);
static_assert(kInputBytes == 393216 && kOutputBlocks == 7500);
static_assert(kInputSamples == HYPER_P14_MAX_INPUT_SAMPLES);
static_assert(kOutputBlocks * kGrain == HYPER_P14_MAX_OUTPUT_SAMPLES);

// No source block, attribute or descriptor is reused while a native context
// could retain it. A single protected mapping owns the entire replay sequence.
struct Wave {
  int16_t samples[kOutputBlocks][kGrain];
  Pcm pcm[kOutputBlocks];
  float gain;
  Attribute attributes[kOutputBlocks][2];
};
constexpr size_t kWaveBytes = (sizeof(Wave) + kPage - 1) / kPage * kPage;

uint64_t monotonic_ms() {
  timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0)
    return UINT64_MAX;
  return static_cast<uint64_t>(now.tv_sec) * 1000u +
         static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}
void erase(void *memory, size_t bytes) {
  auto *p = static_cast<volatile unsigned char *>(memory);
  while (bytes--) *p++ = 0;
}
template <typename Function> Function symbol(uint32_t module, const char *name) {
  static_assert(sizeof(Function) == sizeof(void *));
  Function function = nullptr;
  void *address = hyper_audio_module_symbol(module, name);
  if (address) std::memcpy(&function, &address, sizeof(function));
  return function;
}
struct Buffer {
  void *memory = nullptr;
  size_t bytes = 0;
  bool locked = false;
};
struct Retained {
  bool restart_required = false;
  Buffer input, wave;
  void *context_memory = nullptr;
  size_t context_bytes = 0;
  bool context_locked = false;
  int input_handle = -1;
  uint64_t context = kInvalidHandle, port = kInvalidHandle;
  uintptr_t user_handle = 0;
} retained;

struct Session {
  int user;
  bool (*active)(int);
  bool (*notice)(const char *);
  uint64_t sequence;
  bool (*processing_active)(int);
  bool (*fresh_input)(int);
  bool (*notice_allowed)(int);
  bool processing = false;
  int reply_fd = -1;
  size_t output_blocks = 0, reply_samples = 0;
  uint64_t started = UINT64_MAX, deadline = 0;
  Buffer input_buffer{}, wave_buffer{};
  void *context_memory = nullptr;
  size_t context_bytes = 0;
  bool context_locked = false;
  int input = -1;
  bool input_close_attempted = false, input_close_failed = false;
  uint64_t context = kInvalidHandle, port = kInvalidHandle;
  uintptr_t user_handle = 0;
  bool user_created = false, drained = false;
  unsigned captured = 0, completed = 0, pushes = 0, route_checks = 0;
  uint32_t peak = 0;

  using input_open_fn = int (*)(int, int, int, uint32_t, uint32_t, uint32_t);
  using input_read_fn = int (*)(int, void *);
  using input_handle_fn = int (*)(int);
  using initialize_fn = int (*)();
  using reset_fn = int (*)(ContextParam *);
  using query_fn = int (*)(ContextParam *, size_t *);
  using context_create_fn = int (*)(ContextParam *, void *, size_t, uint64_t *);
  using handle_fn = int (*)(uint64_t);
  using user_create_fn = int (*)(uint32_t, uintptr_t *);
  using user_destroy_fn = int (*)(uintptr_t);
  using port_create_fn = int (*)(uint64_t, PortParam *, uint64_t *);
  using state_fn = int (*)(uint64_t, PortState *);
  using attributes_fn = int (*)(uint64_t, Attribute *, uint32_t);
  using push_fn = int (*)(uint64_t, uint32_t);
  using queue_fn = int (*)(uint64_t, uint32_t *, uint32_t *);
  input_open_fn open_input = nullptr;
  input_read_fn read_input = nullptr;
  input_handle_fn silent = nullptr, close_input = nullptr;
  initialize_fn initialize = nullptr;
  reset_fn reset = nullptr;
  query_fn query = nullptr;
  context_create_fn create_context = nullptr;
  handle_fn destroy_context = nullptr, destroy_port = nullptr, advance = nullptr;
  user_create_fn create_user = nullptr;
  user_destroy_fn destroy_user = nullptr;
  port_create_fn create_port = nullptr;
  state_fn state = nullptr;
  attributes_fn attributes = nullptr;
  push_fn push = nullptr;
  queue_fn queue = nullptr;

  bool alive() const {
    const auto check = processing ? processing_active : active;
    return check && user > 0 && user != 0xfe && user != 0xff && check(user);
  }
  int guard() const {
    if (!alive()) return -ECANCELED;
    const uint64_t now = monotonic_ms();
    if (now == UINT64_MAX || started == UINT64_MAX || now < started) return -EIO;
    return now - started >= deadline ? -ETIMEDOUT : 0;
  }
  int phase(uint64_t milliseconds) {
    started = monotonic_ms(); deadline = milliseconds; return guard();
  }
  int require_fresh_output_input() {
    const uint64_t began = monotonic_ms();
    if (began == UINT64_MAX || !fresh_input || !processing) return -EIO;
    for (;;) {
      const int result = guard();
      if (result < 0) return result;
      if (fresh_input(user)) {
        processing = false;
        return guard();
      }
      const uint64_t now = monotonic_ms();
      if (now == UINT64_MAX || now < began) return -EIO;
      if (now - began >= kHyperVoiceProcessingFreshMs) return -ETIMEDOUT;
      timespec remaining{0, 10000000L};
      while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) return -(errno ? errno : EIO);
        if (guard() < 0) return -ECANCELED;
      }
    }
  }
  int allocate(Buffer &buffer, size_t bytes) {
    buffer.bytes = bytes;
    buffer.memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buffer.memory == MAP_FAILED) {
      buffer.memory = nullptr; return -(errno ? errno : ENOMEM);
    }
    if (reinterpret_cast<uintptr_t>(buffer.memory) % 256u) return -EINVAL;
#ifdef MADV_NOCORE
    if (madvise(buffer.memory, bytes, MADV_NOCORE) != 0)
      return -(errno ? errno : EIO);
#else
    return -ENOTSUP;
#endif
    if (mlock(buffer.memory, bytes) != 0) return -(errno ? errno : EIO);
    buffer.locked = true;
    std::memset(buffer.memory, 0, bytes);
    return guard();
  }
  int prepare() {
    int result = phase(5000);
    if (result < 0) return result;
    uint32_t in_module = 0, out_module = 0;
    result = hyper_audio_module_handle("libSceAudioIn.sprx", &in_module);
    LOG_INFO("Hyper P14 resident AudioIn lookup code=0x%08x handle=0x%08x",
             static_cast<unsigned>(result), static_cast<unsigned>(in_module));
    if (result < 0) return result;
    result = hyper_audio_module_handle("libSceAudioOut.sprx", &out_module);
    LOG_INFO("Hyper P14 resident AudioOut lookup code=0x%08x handle=0x%08x",
             static_cast<unsigned>(result), static_cast<unsigned>(out_module));
    if (result < 0) return result;
    open_input = symbol<input_open_fn>(in_module, "sceAudioInOpen");
    read_input = symbol<input_read_fn>(in_module, "sceAudioInInput");
    silent = symbol<input_handle_fn>(in_module, "sceAudioInGetSilentState");
    close_input = symbol<input_handle_fn>(in_module, "sceAudioInClose");
    initialize = symbol<initialize_fn>(out_module, "sceAudioOut2Initialize");
    reset = symbol<reset_fn>(out_module, "sceAudioOut2ContextResetParam");
    query = symbol<query_fn>(out_module, "sceAudioOut2ContextQueryMemory");
    create_context = symbol<context_create_fn>(out_module, "sceAudioOut2ContextCreate");
    destroy_context = symbol<handle_fn>(out_module, "sceAudioOut2ContextDestroy");
    create_user = symbol<user_create_fn>(out_module, "sceAudioOut2UserCreate");
    destroy_user = symbol<user_destroy_fn>(out_module, "sceAudioOut2UserDestroy");
    create_port = symbol<port_create_fn>(out_module, "sceAudioOut2PortCreate");
    destroy_port = symbol<handle_fn>(out_module, "sceAudioOut2PortDestroy");
    state = symbol<state_fn>(out_module, "sceAudioOut2PortGetState");
    attributes = symbol<attributes_fn>(out_module, "sceAudioOut2PortSetAttributes");
    advance = symbol<handle_fn>(out_module, "sceAudioOut2ContextAdvance");
    push = symbol<push_fn>(out_module, "sceAudioOut2ContextPush");
    queue = symbol<queue_fn>(out_module, "sceAudioOut2ContextGetQueueLevel");
    if (!open_input || !read_input || !silent || !close_input || !initialize ||
        !reset || !query || !create_context || !destroy_context || !create_user ||
        !destroy_user || !create_port || !destroy_port || !state || !attributes ||
        !advance || !push || !queue) return -ENOSYS;
    result = allocate(input_buffer, kInputBytes);
    if (result < 0) return result;
    result = allocate(wave_buffer, kWaveBytes);
    if (result < 0) return result;
    new (wave_buffer.memory) Wave{};
    LOG_INFO("Hyper P14 protected PCM ready input_bytes=%zu output_bytes=%zu", kInputBytes, kWaveBytes);
    return guard();
  }
  int check_capture() {
    int result = guard();
    if (result < 0) return result;
    const int value = silent(input);
    if (value != 0) {
      LOG_WARN("Hyper P14 MIC state rejected code=0x%08x", static_cast<unsigned>(value));
      return value < 0 ? value : -EACCES;
    }
    return guard();
  }
  int capture() {
    if (!notice(hyper_message(HyperMessage::preparing)))
      return -EACCES;
    int result = phase(3000);
    if (result < 0) return result;
    for (;;) {
      if ((result = guard()) < 0) return result;
      const uint64_t now = monotonic_ms();
      if (now == UINT64_MAX || now < started) return -EIO;
      if (now - started >= 2000) break;
      timespec remaining{0, 50000000L};
      while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) return -(errno ? errno : EIO);
        if ((result = guard()) < 0) return result;
      }
    }
    if (!notice(hyper_message(HyperMessage::listening)))
      return -EACCES;
    if ((result = phase(13500)) < 0) return result;
    input = open_input(user, 1, 0, kGrain, 16000, 1);
    LOG_INFO("Hyper P14 MIC open code=0x%08x", static_cast<unsigned>(input));
    if (input < 0) return input;
    auto *pcm = static_cast<int16_t *>(input_buffer.memory);
    unsigned quiet_blocks = 0;
    bool voice_seen = false;
    for (size_t block = 0; block < kInputBlocks; ++block) {
      if ((result = check_capture()) < 0) return result;
      int16_t *destination = pcm + block * kGrain;
      result = read_input(input, destination);
      if (result < 0) return result;
      if ((result = check_capture()) < 0) return result;
      ++captured;
      uint64_t amplitude_sum = 0;
      for (size_t frame = 0; frame < kGrain; ++frame) {
        const int32_t value = destination[frame];
        const uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
        if (magnitude > peak) peak = magnitude;
        amplitude_sum += magnitude;
      }
      if (amplitude_sum / kGrain >= 350) { voice_seen = true; quiet_blocks = 0; }
      else ++quiet_blocks;
      if (voice_seen && captured >= 64 && quiet_blocks >= 75) break;
    }
    return guard();
  }
  int shut_input() {
    if (input < 0) return 0;
    if (input_close_attempted) return input_close_failed ? -EIO : 0;
    input_close_attempted = true;
    const int result = close_input(input);
    LOG_INFO("Hyper P14 MIC close code=0x%08x", static_cast<unsigned>(result));
    input_close_failed = result < 0;
    if (!input_close_failed) input = -1;
    return result;
  }
  int exchange() {
    if (input >= 0 || input_close_failed || captured == 0) return -EIO;
    auto *wave = static_cast<Wave *>(wave_buffer.memory);
    const auto *pcm = static_cast<const int16_t *>(input_buffer.memory);
    const int result = hyper_p14_voice_exchange_session(user, sequence, processing_active, pcm,
        static_cast<size_t>(captured) * kGrain, wave->samples[0],
        kOutputBlocks * kGrain, &reply_samples, &reply_fd);
    if (result < 0) return result;
    if (!reply_samples || reply_samples > kOutputBlocks * kGrain) return -EOVERFLOW;
    output_blocks = (reply_samples + kGrain - 1) / kGrain;
    wave->gain = 0.5f;
    for (size_t block = 0; block < output_blocks; ++block) {
      const int guarded = guard();
      if (guarded < 0) return guarded;
      wave->pcm[block].data = wave->samples[block];
      wave->attributes[block][0] = {1, 0, &wave->gain, sizeof(wave->gain)};
      wave->attributes[block][1] = {0, 0, &wave->pcm[block], sizeof(Pcm)};
    }
    LOG_INFO("Hyper P14 reply received samples=%zu blocks=%zu", reply_samples, output_blocks);
    return guard();
  }
  int check_route(const char *where) {
    int result = guard();
    if (result < 0) return result;
    PortState route{};
    ++route_checks;
    result = state(port, &route);
    if (result < 0) return result;
    if ((result = guard()) < 0) return result;
    if (route_checks == 1 || route.outputs != 4 || route.channels != 1)
      LOG_INFO("Hyper P14 OUT2 route phase=%s outputs=0x%04x channels=%u", where,
               static_cast<unsigned>(route.outputs), static_cast<unsigned>(route.channels));
    return route.outputs == 4 && route.channels == 1 ? 0 : -ENODEV;
  }
  int prepare_output() {
    if (input >= 0 || input_close_failed) return -EIO;
    int result = guard();
    if (result < 0) return result;
    const int initialized = initialize();
    LOG_INFO("Hyper P14 OUT2 initialize code=0x%08x", static_cast<unsigned>(initialized));
    // Like P12, tolerate an already initialized service. No global teardown.
    if ((result = guard()) < 0) return result;
    ContextParam parameters{};
    if ((result = reset(&parameters)) < 0) return result;
    parameters.max_ports = 1; parameters.max_object_ports = 0;
    parameters.guarantee_object_ports = 0; parameters.queue_depth = 1;
    parameters.num_grains = kGrain; parameters.flags = 0;
    std::memset(parameters.reserved, 0, sizeof(parameters.reserved));
    if ((result = guard()) < 0) return result;
    result = query(&parameters, &context_bytes);
    if (result < 0) return result;
    if (!context_bytes || context_bytes > kMaximumContextBytes) return -EOVERFLOW;
    context_memory = mmap(nullptr, context_bytes, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    if (context_memory == MAP_FAILED) {
      context_memory = nullptr; return -(errno ? errno : ENOMEM);
    }
#ifdef MADV_NOCORE
    if (madvise(context_memory, context_bytes, MADV_NOCORE) != 0)
      return -(errno ? errno : EIO);
#else
    return -ENOTSUP;
#endif
    if (mlock(context_memory, context_bytes) != 0)
      return -(errno ? errno : EIO);
    context_locked = true;
    std::memset(context_memory, 0, context_bytes);
    if ((result = guard()) < 0) return result;
    result = create_context(&parameters, context_memory, context_bytes, &context);
    if (result < 0) return result;
    if (context == kInvalidHandle) return -EIO;
    if ((result = guard()) < 0) return result;
    result = create_user(static_cast<uint32_t>(user), &user_handle);
    user_created = user_handle != 0;
    if (result < 0) return result;
    if (!user_created) return -EIO;
    if ((result = guard()) < 0) return result;
    PortParam parameters_port{};
    parameters_port.port_type = 3; parameters_port.data_format = 0x101;
    parameters_port.sampling_frequency = 48000; parameters_port.flags = 1;
    parameters_port.user_handle = user_handle;
    result = create_port(context, &parameters_port, &port);
    LOG_INFO("Hyper P14 OUT2 port create code=0x%08x", static_cast<unsigned>(result));
    if (result < 0) return result;
    if (port == kInvalidHandle) return -EIO;
    return check_route("opened");
  }
  int play() {
    auto *wave = static_cast<Wave *>(wave_buffer.memory);
    for (size_t block = 0; block < output_blocks; ++block) {
      int result = guard();
      if (result < 0) return result;
      if ((result = attributes(port, wave->attributes[block], 2)) < 0) return result;
      if ((result = guard()) < 0) return result;
      if ((result = advance(context)) < 0) return result;
      if ((result = check_route("before-push")) < 0) return result;
      ++pushes;
      const int pushed = push(context, 1);
      const int after = check_route("after-push");
      if (pushed < 0) return pushed;
      if (after < 0) return after;
      ++completed;
    }
    for (;;) {
      int result = check_route("drain-before");
      if (result < 0) return result;
      uint32_t queued = 0, available = 0;
      if ((result = queue(context, &queued, &available)) < 0) return result;
      if ((result = check_route("drain-after")) < 0) return result;
      if (!queued) { drained = true; return 0; }
      timespec remaining{0, 1000000L};
      while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) return -(errno ? errno : EIO);
        if ((result = guard()) < 0) return result;
      }
    }
  }
  bool release(Buffer &buffer) {
    if (!buffer.memory) return true;
    erase(buffer.memory, buffer.bytes);
    if (buffer.locked) {
      if (munlock(buffer.memory, buffer.bytes) != 0) return false;
      buffer.locked = false;
    }
    if (munmap(buffer.memory, buffer.bytes) != 0) return false;
    buffer.memory = nullptr;
    return true;
  }
  bool retain(const char *why) {
    retained = {true, input_buffer, wave_buffer, context_memory, context_bytes,
                context_locked, input, context, port, user_handle};
    LOG_WARN("Hyper P14 cleanup retained reason=%s pcm_erased=0; reboot required", why);
    return false;
  }
  bool cleanup() {
    if (shut_input() < 0) return retain("microphone close");
    if (port != kInvalidHandle) {
      const int result = destroy_port(port);
      LOG_INFO("Hyper P14 OUT2 port destroy code=0x%08x", static_cast<unsigned>(result));
      if (result >= 0) port = kInvalidHandle;
    }
    if (context != kInvalidHandle) {
      const int result = destroy_context(context);
      LOG_INFO("Hyper P14 OUT2 context destroy code=0x%08x", static_cast<unsigned>(result));
      if (result < 0) return retain("context destroy");
      context = port = kInvalidHandle;
    }
    if (!release(input_buffer)) return retain("input memory release");
    if (!release(wave_buffer)) return retain("output memory release");
    if (context_memory) {
      erase(context_memory, context_bytes);
      if (context_locked) {
        if (munlock(context_memory, context_bytes) != 0)
          return retain("context memory unlock");
        context_locked = false;
      }
      if (munmap(context_memory, context_bytes) != 0) return retain("context memory release");
      context_memory = nullptr;
    }
    if (user_created) {
      const int result = destroy_user(user_handle);
      LOG_INFO("Hyper P14 OUT2 user destroy code=0x%08x", static_cast<unsigned>(result));
      if (result < 0) return retain("user destroy");
      user_created = false; user_handle = 0;
    }
    return true;
  }
};
} // namespace

void hyper_p14_voice_assistant(int user, uint64_t sequence, bool (*active)(int),
                         bool (*notice)(const char *), bool (*processing_active)(int),
                         bool (*fresh_input)(int), bool (*notice_allowed)(int)) {
  Session session{user, active, notice, sequence, processing_active, fresh_input, notice_allowed};
  if (!notice || !processing_active || !fresh_input || !notice_allowed || !session.alive()) return;
  if (retained.restart_required) {
    notice(hyper_message(HyperMessage::restart_required));
    return;
  }
  int result = session.prepare();
  if (result >= 0) result = session.capture();
  const int closed = session.shut_input();
  if (closed < 0) result = closed;
  LOG_INFO("Hyper P14 capture code=0x%08x blocks=%u peak=%u", static_cast<unsigned>(result), session.captured, session.peak);
  if (result >= 0 && session.peak < 64) result = -ENOMSG;
  if (result >= 0) {
    if (!notice(hyper_message(HyperMessage::thinking))) result = -EACCES;
    else {
      session.processing = true;
      result = session.phase(180000);
    }
  }
  if (result >= 0) result = session.exchange();
  if (result >= 0) result = session.require_fresh_output_input();
  if (result >= 0) result = session.phase(45000);
  if (result >= 0) result = session.prepare_output();
  if (result >= 0) result = session.play();
  const bool released = session.cleanup();
  const int done_status = released && session.drained && result >= 0 ? 0 :
                          (result < 0 ? result : -EIO);
  const int done_result = hyper_p14_voice_finish(&session.reply_fd, user, sequence,
      active, done_status, done_status == 0 ? session.reply_samples : 0);
  LOG_INFO("Hyper P14 playback acknowledgement code=0x%08x", static_cast<unsigned>(done_result));
  LOG_INFO("Hyper P14 finished code=0x%08x captured=%u pushes=%u completed=%u route_checks=%u drained=%u cleanup=%s pcm_erased=%u",
           static_cast<unsigned>(result), session.captured, session.pushes,
           session.completed, session.route_checks, session.drained ? 1u : 0u,
           released ? "complete" : "retained", released ? 1u : 0u);
  if (!session.notice_allowed(user)) return;
  if (!released) notice(hyper_message(HyperMessage::restart_required));
  else if (result >= 0 && session.completed == session.output_blocks && session.drained)
    notice(hyper_message(HyperMessage::finished));
  else notice(hyper_message(HyperMessage::interrupted));
}
