#include "hyper_output_route_probe.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <time.h>

#include <onion/log.h>

namespace {
constexpr uint64_t kInvalidHandle = UINT64_MAX;
constexpr size_t kMaximumContextBytes = 4u * 1024u * 1024u;

// ABI from the locally supplied SharpProspero AudioOut2.cs definitions.
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

struct alignas(8) LegacyPortState {
  uint16_t outputs;
  uint8_t channels;
  uint8_t reserved[29];
};

static_assert(sizeof(ContextParam) == 64);
static_assert(sizeof(PortParam) == 64);
static_assert(sizeof(PortState) == 64);
static_assert(sizeof(LegacyPortState) == 32);
static_assert(offsetof(ContextParam, flags) == 20);
static_assert(offsetof(PortParam, user_handle) == 16);
static_assert(offsetof(PortState, channels) == 2);
static_assert(offsetof(PortState, reroute_counter) == 6);
static_assert(offsetof(PortState, flags) == 8);

template <typename Function>
Function resolve(void *module, const char *name) {
  static_assert(sizeof(Function) == sizeof(void *));
  Function function = nullptr;
  void *symbol = dlsym(module, name);
  if (symbol) std::memcpy(&function, &symbol, sizeof(function));
  return function;
}

// A failed native destruction must not leave the context referencing freed
// caller memory or an unloaded module. Retain those resources until reboot.
struct RetainedResources {
  bool restart_required = false;
  void *module = nullptr;
  void *memory = nullptr;
  size_t memory_bytes = 0;
  uint64_t context = kInvalidHandle;
  uint64_t port = kInvalidHandle;
  uintptr_t user_handle = 0;
  int legacy_port = -1;
};
RetainedResources retained;

struct Session {
  int user;
  bool (*active)(int);
  bool (*notice)(const char *);
  void *module = nullptr;
  void *memory = nullptr;
  size_t memory_bytes = 0;
  uint64_t context = kInvalidHandle;
  uint64_t port = kInvalidHandle;
  uintptr_t user_handle = 0;
  bool user_created = false;
  int legacy_port = -1;
  bool legacy_close_failed = false;

  using initialize_fn = int (*)();
  using reset_fn = int (*)(ContextParam *);
  using query_fn = int (*)(ContextParam *, size_t *);
  using create_context_fn = int (*)(ContextParam *, void *, size_t, uint64_t *);
  using destroy_context_fn = int (*)(uint64_t);
  using create_user_fn = int (*)(uint32_t, uintptr_t *);
  using destroy_user_fn = int (*)(uintptr_t);
  using create_port_fn = int (*)(uint64_t, PortParam *, uint64_t *);
  using destroy_port_fn = int (*)(uint64_t);
  using state_fn = int (*)(uint64_t, PortState *);
  using legacy_open_fn = int (*)(int, int, int, uint32_t, uint32_t, uint32_t);
  using legacy_state_fn = int (*)(int, LegacyPortState *);
  using legacy_close_fn = int (*)(int);
  initialize_fn initialize = nullptr;
  reset_fn reset = nullptr;
  query_fn query = nullptr;
  create_context_fn create_context = nullptr;
  destroy_context_fn destroy_context = nullptr;
  create_user_fn create_user = nullptr;
  destroy_user_fn destroy_user = nullptr;
  create_port_fn create_port = nullptr;
  destroy_port_fn destroy_port = nullptr;
  state_fn state = nullptr;
  legacy_close_fn close_legacy = nullptr;

  bool alive() const { return active && user >= 0 && active(user); }

  int prepare() {
    if (!alive()) return -ECANCELED;
    module = dlopen("libSceAudioOut.sprx", RTLD_LAZY | RTLD_LOCAL);
    if (!module) return -ENOSYS;
    initialize = resolve<initialize_fn>(module, "sceAudioOut2Initialize");
    reset = resolve<reset_fn>(module, "sceAudioOut2ContextResetParam");
    query = resolve<query_fn>(module, "sceAudioOut2ContextQueryMemory");
    create_context = resolve<create_context_fn>(module, "sceAudioOut2ContextCreate");
    destroy_context = resolve<destroy_context_fn>(module, "sceAudioOut2ContextDestroy");
    create_user = resolve<create_user_fn>(module, "sceAudioOut2UserCreate");
    destroy_user = resolve<destroy_user_fn>(module, "sceAudioOut2UserDestroy");
    create_port = resolve<create_port_fn>(module, "sceAudioOut2PortCreate");
    destroy_port = resolve<destroy_port_fn>(module, "sceAudioOut2PortDestroy");
    state = resolve<state_fn>(module, "sceAudioOut2PortGetState");
    if (!initialize || !reset || !query || !create_context || !destroy_context ||
        !create_user || !destroy_user || !create_port || !destroy_port || !state)
      return -ENOSYS;
    const auto legacy_initialize = resolve<initialize_fn>(module, "sceAudioOutInit");
    const auto legacy_open = resolve<legacy_open_fn>(module, "sceAudioOutOpen");
    const auto legacy_state = resolve<legacy_state_fn>(module, "sceAudioOutGetPortState");
    close_legacy = resolve<legacy_close_fn>(module, "sceAudioOutClose");
    if (!legacy_initialize || !legacy_open || !legacy_state || !close_legacy)
      return -ENOSYS;
    if (!alive()) return -ECANCELED;
    int result = legacy_initialize();
    LOG_INFO("Hyper P11 OUT classic initialize code=0x%08x",
             static_cast<unsigned>(result));
    if (!alive()) return -ECANCELED;
    legacy_port = legacy_open(user, 4, 0, 1024, 48000, 0);
    LOG_INFO("Hyper P11 OUT classic open code=0x%08x",
             static_cast<unsigned>(legacy_port));
    if (legacy_port >= 0) {
      if (alive()) {
        LegacyPortState route{};
        result = legacy_state(legacy_port, &route);
        LOG_INFO("Hyper P11 OUT classic state code=0x%08x outputs=0x%04x channels=%u",
                 static_cast<unsigned>(result),
                 static_cast<unsigned>(route.outputs),
                 static_cast<unsigned>(route.channels));
      }
      result = close_legacy(legacy_port);
      LOG_INFO("Hyper P11 OUT classic close code=0x%08x",
               static_cast<unsigned>(result));
      legacy_close_failed = result < 0;
      if (legacy_close_failed) return result;
      legacy_port = -1;
    }
    if (!alive()) return -ECANCELED;
    result = initialize();
    LOG_INFO("Hyper P11 OUT2 initialize code=0x%08x",
             static_cast<unsigned>(result));
    // Local AudioQueueDevice tolerates an already-initialized service. Every
    // subsequent owned-resource operation is still checked individually.
    if (!alive()) return -ECANCELED;
    ContextParam parameters{};
    result = reset(&parameters);
    LOG_INFO("Hyper P11 OUT2 reset code=0x%08x", static_cast<unsigned>(result));
    if (result < 0) return result;
    parameters.max_ports = 1;
    parameters.max_object_ports = 0;
    parameters.guarantee_object_ports = 0;
    parameters.queue_depth = 1;
    parameters.num_grains = 256;
    parameters.flags = 0;
    std::memset(parameters.reserved, 0, sizeof(parameters.reserved));
    if (!alive()) return -ECANCELED;
    result = query(&parameters, &memory_bytes);
    LOG_INFO("Hyper P11 OUT2 query code=0x%08x bytes=%zu",
             static_cast<unsigned>(result), memory_bytes);
    if (result < 0) return result;
    if (!memory_bytes || memory_bytes > kMaximumContextBytes) return -EOVERFLOW;
    memory = mmap(nullptr, memory_bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) {
      memory = nullptr;
      return -(errno ? errno : ENOMEM);
    }
    std::memset(memory, 0, memory_bytes);
    if (!alive()) return -ECANCELED;
    result = create_context(&parameters, memory, memory_bytes, &context);
    LOG_INFO("Hyper P11 OUT2 context create code=0x%08x handle=0x%016llx",
             static_cast<unsigned>(result),
             static_cast<unsigned long long>(context));
    if (result < 0) return result;
    if (context == kInvalidHandle) return -EIO;
    if (!alive()) return -ECANCELED;
    result = create_user(static_cast<uint32_t>(user), &user_handle);
    LOG_INFO("Hyper P11 OUT2 user create code=0x%08x handle=0x%016llx",
             static_cast<unsigned>(result),
             static_cast<unsigned long long>(user_handle));
    user_created = result >= 0 && user_handle != 0;
    if (result < 0) return result;
    if (!user_handle) return -EIO;
    return alive() ? 0 : -ECANCELED;
  }

  int close_port() {
    if (port == kInvalidHandle) return 0;
    const int result = destroy_port(port);
    LOG_INFO("Hyper P11 OUT2 port destroy code=0x%08x",
             static_cast<unsigned>(result));
    if (result >= 0) port = kInvalidHandle;
    return result;
  }

  int wait_between_reads() const {
    timespec remaining{0, 100000000};
    while (alive()) {
      if (nanosleep(&remaining, &remaining) == 0)
        return alive() ? 0 : -ECANCELED;
      if (errno != EINTR) return -(errno ? errno : EIO);
    }
    return -ECANCELED;
  }

  int probe_port(uint32_t flags) {
    if (!alive()) return -ECANCELED;
    PortParam parameters{};
    parameters.port_type = 3;
    parameters.data_format = 0x101;
    parameters.sampling_frequency = 48000;
    parameters.flags = flags;
    parameters.user_handle = user_handle;
    int result = create_port(context, &parameters, &port);
    LOG_INFO("Hyper P11 OUT2 port create requested_flags=%u code=0x%08x handle=0x%016llx",
             static_cast<unsigned>(flags), static_cast<unsigned>(result),
             static_cast<unsigned long long>(port));
    if (result < 0) return result;
    if (port == kInvalidHandle) return -EIO;
    for (unsigned read = 0; read < 3; ++read) {
      if (!alive()) return -ECANCELED;
      PortState route{};
      result = state(port, &route);
      LOG_INFO("Hyper P11 OUT2 state requested_flags=%u read=%u code=0x%08x outputs=0x%04x channels=%u flags=0x%08x reroute=%u volume=%d",
               static_cast<unsigned>(flags), read + 1,
               static_cast<unsigned>(result), static_cast<unsigned>(route.outputs),
               static_cast<unsigned>(route.channels), static_cast<unsigned>(route.flags),
               static_cast<unsigned>(route.reroute_counter), static_cast<int>(route.volume));
      if (result < 0) return result;
      if (!alive()) return -ECANCELED;
      if (read != 2) {
        result = wait_between_reads();
        if (result < 0) return result;
      }
    }
    return 0;
  }

  bool cleanup() {
    if (legacy_close_failed) {
      retained = {true, module, nullptr, 0, kInvalidHandle, kInvalidHandle, 0};
      retained.legacy_port = legacy_port;
      LOG_WARN("Hyper P11 OUT classic remains open; module retained; reboot required");
      return false;
    }
    close_port();
    if (context != kInvalidHandle) {
      const int result = destroy_context(context);
      LOG_INFO("Hyper P11 OUT2 context destroy code=0x%08x",
               static_cast<unsigned>(result));
      if (result < 0) {
        retained = {true, module, memory, memory_bytes, context, port, user_handle};
        LOG_WARN("Hyper P11 OUT2 context remains live; memory, user and module retained; reboot required");
        return false;
      }
      context = kInvalidHandle;
      port = kInvalidHandle;
    }
    if (memory) {
      const int result = munmap(memory, memory_bytes);
      LOG_INFO("Hyper P11 OUT2 memory release result=%d", result);
      if (result != 0) {
        retained = {true, module, memory, memory_bytes, kInvalidHandle,
                    kInvalidHandle, user_handle};
        LOG_WARN("Hyper P11 OUT2 memory release failed; allocation, user and module retained; reboot required");
        return false;
      }
      memory = nullptr;
    }
    if (user_created) {
      const int result = destroy_user(user_handle);
      LOG_INFO("Hyper P11 OUT2 user destroy code=0x%08x",
               static_cast<unsigned>(result));
      if (result < 0) {
        retained = {true, module, nullptr, 0, kInvalidHandle, kInvalidHandle, user_handle};
        LOG_WARN("Hyper P11 OUT2 user remains live; module retained; reboot required");
        return false;
      }
      user_created = false;
      user_handle = 0;
    }
    if (module) {
      dlclose(module);
      module = nullptr;
    }
    return true;
  }
};
} // namespace

void hyper_p11_probe_output_route(int user, bool (*session_active)(int),
                                  bool (*notice)(const char *)) {
  Session session{user, session_active, notice};
  if (!notice || !session.alive()) return;
  if (retained.restart_required) {
    notice("Hyper P11: riavvio completo necessario prima di un'altra prova.");
    return;
  }
  if (!notice("Hyper P11: verifica delle uscite DualSense, senza acquisire o riprodurre audio."))
    return;
  int result = session.prepare();
  unsigned completed = 0;
  if (result >= 0) {
    // Bit 0 is documented locally only as 'held back'. This probe makes no
    // assumption that this flag excludes any particular external output.
    for (uint32_t flags = 0; flags <= 1; ++flags) {
      const int probe_result = session.probe_port(flags);
      const int close_result = session.close_port();
      if (probe_result >= 0) ++completed;
      if (probe_result < 0 && result >= 0) result = probe_result;
      if (close_result < 0) {
        result = close_result;
        break;
      }
      if (!session.alive()) {
        result = -ECANCELED;
        break;
      }
    }
  }
  const bool released = session.cleanup();
  LOG_INFO("Hyper P11 OUT2 probe completed=%u code=0x%08x cleanup=%s",
           completed, static_cast<unsigned>(result), released ? "complete" : "retained");
  if (!session.alive()) return;
  if (!released)
    notice("Hyper P11: chiusura incompleta. Esegui un riavvio completo prima di riprovare.");
  else if (result >= 0 && completed == 2)
    notice("Hyper P11: verifica uscite completata. Invia il klog.");
  else
    notice("Hyper P11: verifica uscite interrotta. Invia il klog.");
}
