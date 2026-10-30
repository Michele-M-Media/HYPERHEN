#include "hyper_output_tone.hpp"
#include "hyper_audio_module.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <time.h>

#include <onion/log.h>

namespace {
constexpr uint64_t kInvalidHandle = UINT64_MAX;
constexpr size_t kMaximumContextBytes = 4u * 1024u * 1024u;
constexpr size_t kGrain = 256;
constexpr size_t kToneBlocks = 32;
constexpr size_t kPauseBlocks = 16;
constexpr size_t kBlocksPerTone = kToneBlocks + kPauseBlocks;
constexpr size_t kBlocks = 3 * kBlocksPerTone;
constexpr size_t kToneSamples = kToneBlocks * kGrain;
constexpr uint64_t kDeadlineMs = 5000;
constexpr size_t kPageBytes = 16384;

// Exact layouts and entry-point signatures from local AudioOut2.cs and
// AudioQueueDevice.cs. No mastering or application-global output settings.
struct ContextParam {
  uint32_t max_ports;
  uint32_t max_object_ports;
  uint32_t guarantee_object_ports;
  uint32_t queue_depth;
  uint32_t num_grains;
  uint32_t flags;
  uint32_t reserved[10];
};
struct PortParam {
  uint16_t port_type;
  uint16_t pad;
  uint32_t data_format;
  uint32_t sampling_frequency;
  uint32_t flags;
  uintptr_t user_handle;
  uint32_t reserved[10];
};
struct PortState {
  uint16_t outputs;
  uint8_t channels;
  uint8_t pad1;
  int16_t volume;
  uint16_t reroute_counter;
  uint32_t flags;
  uint32_t pad2;
  uint64_t reserved[6];
};
struct Attribute {
  uint32_t id;
  uint32_t pad;
  void *value;
  size_t value_size;
};
struct Pcm {
  void *data;
};
static_assert(sizeof(ContextParam) == 64);
static_assert(offsetof(ContextParam, flags) == 20);
static_assert(sizeof(PortParam) == 64);
static_assert(offsetof(PortParam, user_handle) == 16);
static_assert(sizeof(PortState) == 64);
static_assert(offsetof(PortState, channels) == 2);
static_assert(offsetof(PortState, reroute_counter) == 6);
static_assert(offsetof(PortState, flags) == 8);
static_assert(sizeof(Attribute) == 24);
static_assert(offsetof(Attribute, value) == 8);
static_assert(offsetof(Attribute, value_size) == 16);
static_assert(sizeof(Pcm) == 8);
static_assert(sizeof(float) == 4);
static_assert(kBlocks == 144);

// Each block and its descriptors remain at distinct, stable addresses until
// destruction of the owned context succeeds, including the failure path.
struct WaveStorage {
  int16_t samples[kBlocks][kGrain];
  Pcm pcm[kBlocks];
  float gain;
  Attribute attributes[kBlocks][2];
};
constexpr size_t kWaveBytes =
    (sizeof(WaveStorage) + kPageBytes - 1) / kPageBytes * kPageBytes;
static_assert(kWaveBytes == 81920);

uint64_t monotonic_ms() {
  timespec timestamp{};
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0 || timestamp.tv_sec < 0)
    return UINT64_MAX;
  return static_cast<uint64_t>(timestamp.tv_sec) * 1000u +
         static_cast<uint64_t>(timestamp.tv_nsec) / 1000000u;
}

template <typename Function>
Function resolve(uint32_t module, const char *name) {
  static_assert(sizeof(Function) == sizeof(void *));
  Function function = nullptr;
  void *symbol = hyper_audio_module_symbol(module, name);
  if (symbol) std::memcpy(&function, &symbol, sizeof(function));
  return function;
}

struct RetainedResources {
  bool restart_required = false;
  void *context_memory = nullptr;
  size_t context_bytes = 0;
  WaveStorage *wave = nullptr;
  uint64_t context = kInvalidHandle;
  uint64_t port = kInvalidHandle;
  uintptr_t user_handle = 0;
};
RetainedResources retained;

struct Session {
  int user;
  bool (*active)(int);
  uint64_t started = UINT64_MAX;
  uint32_t module = 0;
  void *context_memory = nullptr;
  size_t context_bytes = 0;
  WaveStorage *wave = nullptr;
  uint64_t context = kInvalidHandle;
  uint64_t port = kInvalidHandle;
  uintptr_t user_handle = 0;
  bool user_created = false;
  unsigned pushes = 0;
  unsigned completed = 0;
  unsigned route_checks = 0;
  unsigned queue_checks = 0;
  bool drained = false;

  using initialize_fn = int (*)();
  using reset_fn = int (*)(ContextParam *);
  using query_fn = int (*)(ContextParam *, size_t *);
  using create_context_fn = int (*)(ContextParam *, void *, size_t, uint64_t *);
  using handle_fn = int (*)(uint64_t);
  using create_user_fn = int (*)(uint32_t, uintptr_t *);
  using destroy_user_fn = int (*)(uintptr_t);
  using create_port_fn = int (*)(uint64_t, PortParam *, uint64_t *);
  using state_fn = int (*)(uint64_t, PortState *);
  using attributes_fn = int (*)(uint64_t, Attribute *, uint32_t);
  using push_fn = int (*)(uint64_t, uint32_t);
  using queue_fn = int (*)(uint64_t, uint32_t *, uint32_t *);
  initialize_fn initialize = nullptr;
  reset_fn reset = nullptr;
  query_fn query = nullptr;
  create_context_fn create_context = nullptr;
  handle_fn destroy_context = nullptr;
  create_user_fn create_user = nullptr;
  destroy_user_fn destroy_user = nullptr;
  create_port_fn create_port = nullptr;
  handle_fn destroy_port = nullptr;
  state_fn state = nullptr;
  attributes_fn attributes = nullptr;
  handle_fn advance = nullptr;
  push_fn push = nullptr;
  queue_fn queue = nullptr;

  bool alive() const {
    return active && user > 0 && user != 0xfe && user != 0xff && active(user);
  }

  int guard() const {
    if (!alive()) return -ECANCELED;
    const uint64_t now = monotonic_ms();
    if (now == UINT64_MAX || started == UINT64_MAX || now < started) return -EIO;
    return now - started >= kDeadlineMs ? -ETIMEDOUT : 0;
  }

  int check_route(const char *phase) {
    int result = guard();
    if (result < 0) return result;
    PortState route{};
    ++route_checks;
    result = state(port, &route);
    if (result < 0) {
      LOG_WARN("Hyper P12 OUT2 route API phase=%s code=0x%08x",
               phase, static_cast<unsigned>(result));
      return result;
    }
    result = guard();
    if (result < 0) return result;
    if (route_checks == 1 || route.outputs != 4 || route.channels != 1)
      LOG_INFO("Hyper P12 OUT2 route phase=%s outputs=0x%04x channels=%u flags=0x%08x reroute=%u",
               phase, static_cast<unsigned>(route.outputs),
               static_cast<unsigned>(route.channels),
               static_cast<unsigned>(route.flags),
               static_cast<unsigned>(route.reroute_counter));
    if (route.outputs != 4 || route.channels != 1) return -ENODEV;
    return 0;
  }

  void generate_wave() {
    static constexpr uint32_t frequencies[3] = {440, 660, 880};
    constexpr size_t fade_samples = 96;
    wave->gain = 0.5f;
    for (size_t block = 0; block < kBlocks; ++block) {
      const size_t segment = block / kBlocksPerTone;
      const size_t local_block = block % kBlocksPerTone;
      if (local_block < kToneBlocks) {
        for (size_t sample = 0; sample < kGrain; ++sample) {
          const size_t index = local_block * kGrain + sample;
          const uint32_t phase = static_cast<uint32_t>(
              (static_cast<uint64_t>(index) * frequencies[segment] + 12000u) % 48000u);
          const uint32_t ramp = phase <= 24000u ? phase : 48000u - phase;
          int32_t amplitude = static_cast<int32_t>(ramp * 8192u / 24000u) - 4096;
          size_t envelope = index + 1;
          if (kToneSamples - index < envelope) envelope = kToneSamples - index;
          if (envelope < fade_samples)
            amplitude = amplitude * static_cast<int32_t>(envelope) /
                        static_cast<int32_t>(fade_samples);
          wave->samples[block][sample] = static_cast<int16_t>(amplitude);
        }
      }
      wave->pcm[block].data = wave->samples[block];
      wave->attributes[block][0] = {1, 0, &wave->gain, sizeof(wave->gain)};
      wave->attributes[block][1] = {0, 0, &wave->pcm[block], sizeof(Pcm)};
    }
  }

  int prepare() {
    int result = guard();
    if (result < 0) return result;
    result = hyper_audio_module_handle("libSceAudioOut.sprx", &module);
    if (result != 0) {
      LOG_WARN("Hyper P12 OUT2 resident module unavailable code=0x%08x",
               static_cast<unsigned>(result));
      return result;
    }
    initialize = resolve<initialize_fn>(module, "sceAudioOut2Initialize");
    reset = resolve<reset_fn>(module, "sceAudioOut2ContextResetParam");
    query = resolve<query_fn>(module, "sceAudioOut2ContextQueryMemory");
    create_context = resolve<create_context_fn>(module, "sceAudioOut2ContextCreate");
    destroy_context = resolve<handle_fn>(module, "sceAudioOut2ContextDestroy");
    create_user = resolve<create_user_fn>(module, "sceAudioOut2UserCreate");
    destroy_user = resolve<destroy_user_fn>(module, "sceAudioOut2UserDestroy");
    create_port = resolve<create_port_fn>(module, "sceAudioOut2PortCreate");
    destroy_port = resolve<handle_fn>(module, "sceAudioOut2PortDestroy");
    state = resolve<state_fn>(module, "sceAudioOut2PortGetState");
    attributes = resolve<attributes_fn>(module, "sceAudioOut2PortSetAttributes");
    advance = resolve<handle_fn>(module, "sceAudioOut2ContextAdvance");
    push = resolve<push_fn>(module, "sceAudioOut2ContextPush");
    queue = resolve<queue_fn>(module, "sceAudioOut2ContextGetQueueLevel");
    if (!initialize || !reset || !query || !create_context || !destroy_context ||
        !create_user || !destroy_user || !create_port || !destroy_port ||
        !state || !attributes || !advance || !push || !queue)
      return -ENOSYS;
    result = guard();
    if (result < 0) return result;
    result = initialize();
    LOG_INFO("Hyper P12 OUT2 initialize code=0x%08x", static_cast<unsigned>(result));
    // The local wrapper tolerates an already-initialized service. Individual
    // resource creation still must succeed; this module never tears it down.
    if ((result = guard()) < 0) return result;
    ContextParam parameters{};
    result = reset(&parameters);
    LOG_INFO("Hyper P12 OUT2 reset code=0x%08x", static_cast<unsigned>(result));
    if (result < 0) return result;
    parameters.max_ports = 1;
    parameters.max_object_ports = 0;
    parameters.guarantee_object_ports = 0;
    parameters.queue_depth = 1;
    parameters.num_grains = kGrain;
    parameters.flags = 0;
    std::memset(parameters.reserved, 0, sizeof(parameters.reserved));
    if ((result = guard()) < 0) return result;
    result = query(&parameters, &context_bytes);
    LOG_INFO("Hyper P12 OUT2 query code=0x%08x bytes=%zu",
             static_cast<unsigned>(result), context_bytes);
    if (result < 0) return result;
    if (!context_bytes || context_bytes > kMaximumContextBytes) return -EOVERFLOW;
    context_memory = mmap(nullptr, context_bytes, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    if (context_memory == MAP_FAILED) {
      context_memory = nullptr;
      return -(errno ? errno : ENOMEM);
    }
    std::memset(context_memory, 0, context_bytes);
    void *wave_memory = mmap(nullptr, kWaveBytes, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON, -1, 0);
    if (wave_memory == MAP_FAILED) return -(errno ? errno : ENOMEM);
    wave = new (wave_memory) WaveStorage{};
    generate_wave();
    if ((result = guard()) < 0) return result;
    result = create_context(&parameters, context_memory, context_bytes, &context);
    LOG_INFO("Hyper P12 OUT2 context create code=0x%08x handle=0x%016llx",
             static_cast<unsigned>(result), static_cast<unsigned long long>(context));
    if (result < 0) return result;
    if (context == kInvalidHandle) return -EIO;
    if ((result = guard()) < 0) return result;
    result = create_user(static_cast<uint32_t>(user), &user_handle);
    LOG_INFO("Hyper P12 OUT2 user create code=0x%08x handle=0x%016llx",
             static_cast<unsigned>(result), static_cast<unsigned long long>(user_handle));
    user_created = user_handle != 0;
    if (result < 0) return result;
    if (!user_handle) return -EIO;
    if ((result = guard()) < 0) return result;
    PortParam port_parameters{};
    port_parameters.port_type = 3;
    port_parameters.data_format = 0x101;
    port_parameters.sampling_frequency = 48000;
    port_parameters.flags = 1;
    port_parameters.user_handle = user_handle;
    result = create_port(context, &port_parameters, &port);
    LOG_INFO("Hyper P12 OUT2 port create held_back=1 code=0x%08x handle=0x%016llx",
             static_cast<unsigned>(result), static_cast<unsigned long long>(port));
    if (result < 0) return result;
    if (port == kInvalidHandle) return -EIO;
    return check_route("opened");
  }

  int play() {
    for (size_t block = 0; block < kBlocks; ++block) {
      int result = guard();
      if (result < 0) return result;
      result = attributes(port, wave->attributes[block], 2);
      if (result < 0) return result;
      if ((result = guard()) < 0) return result;
      result = advance(context);
      if (result < 0) return result;
      result = check_route("before-push");
      if (result < 0) return result;
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
      ++queue_checks;
      result = queue(context, &queued, &available);
      if (result < 0) return result;
      result = check_route("drain-after");
      if (result < 0) return result;
      if (!queued) {
        drained = true;
        return 0;
      }
      timespec remaining{0, 1000000};
      while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) return -(errno ? errno : EIO);
        if ((result = guard()) < 0) return result;
      }
    }
  }

  bool retain(const char *reason) {
    retained = {true, context_memory, context_bytes, wave,
                context, port, user_handle};
    LOG_WARN("Hyper P12 OUT2 cleanup retained reason=%s; reboot required", reason);
    return false;
  }

  bool cleanup() {
    if (port != kInvalidHandle) {
      const int result = destroy_port(port);
      LOG_INFO("Hyper P12 OUT2 port destroy code=0x%08x", static_cast<unsigned>(result));
      if (result >= 0) port = kInvalidHandle;
    }
    if (context != kInvalidHandle) {
      const int result = destroy_context(context);
      LOG_INFO("Hyper P12 OUT2 context destroy code=0x%08x", static_cast<unsigned>(result));
      if (result < 0) return retain("context destroy");
      context = kInvalidHandle;
      port = kInvalidHandle;
    }
    if (context_memory) {
      const int result = munmap(context_memory, context_bytes);
      LOG_INFO("Hyper P12 OUT2 context memory release result=%d", result);
      if (result != 0) return retain("context memory release");
      context_memory = nullptr;
    }
    if (wave) {
      const int result = munmap(wave, kWaveBytes);
      LOG_INFO("Hyper P12 OUT2 wave memory release result=%d", result);
      if (result != 0) return retain("wave memory release");
      wave = nullptr;
    }
    if (user_created) {
      const int result = destroy_user(user_handle);
      LOG_INFO("Hyper P12 OUT2 user destroy code=0x%08x", static_cast<unsigned>(result));
      if (result < 0) return retain("user destroy");
      user_created = false;
      user_handle = 0;
    }
    // AudioOut is borrowed from ShellUI. Release only our owned audio resources;
    // unloading a process-resident system library is never part of this session.
    return true;
  }
};
} // namespace

void hyper_p12_output_tone(int user, bool (*active)(int),
                           bool (*notice)(const char *)) {
  Session session{user, active};
  if (!notice || !session.alive()) return;
  if (retained.restart_required) {
    notice("Hyper P12: riavvio completo necessario prima di un'altra prova.");
    return;
  }
  if (!notice("Hyper P12: prova di tre toni brevi dal DualSense. Microfono chiuso."))
    return;
  session.started = monotonic_ms();
  int result = session.prepare();
  if (result >= 0) result = session.play();
  const bool released = session.cleanup();
  LOG_INFO("Hyper P12 OUT2 finished code=0x%08x pushes=%u completed=%u route_checks=%u queue_checks=%u drained=%u cleanup=%s",
           static_cast<unsigned>(result), session.pushes, session.completed,
           session.route_checks, session.queue_checks, session.drained ? 1u : 0u,
           released ? "complete" : "retained");
  if (!session.alive()) return;
  if (!released)
    notice("Hyper P12: chiusura incompleta. Esegui un riavvio completo prima di riprovare.");
  else if (result >= 0 && session.completed == kBlocks && session.drained)
    notice("Hyper P12: tre toni completati. Invia il klog e indica se li hai sentiti.");
  else
    notice("Hyper P12: prova dei toni interrotta. Invia il klog.");
}
