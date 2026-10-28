#include "hyper_audio_echo.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <sys/mman.h>
#include <time.h>

#include <onion/log.h>

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int, int, int, uint32_t, uint32_t, uint32_t);
int sceAudioOutSetVolume(int, int, const int32_t *);
int sceAudioOutOutput(int, const void *);
int sceAudioOutClose(int);
}

namespace {
constexpr size_t kInputGrain = 256;
constexpr size_t kInputBlocks = 124;
constexpr size_t kInputSamples = kInputBlocks * kInputGrain;
constexpr size_t kOutputGrain = 1024;
constexpr size_t kMemoryBytes = 65536;
constexpr uint64_t kCaptureLimitMs = 3500;
constexpr uint64_t kPlaybackLimitMs = 5000;
static_assert((kInputSamples + kOutputGrain) * sizeof(int16_t) ==
              kMemoryBytes);
static_assert((kInputSamples * 3) % kOutputGrain == 0);

// Local SharpProspero AudioOutPortState ABI: 32 bytes, fields at 0 and 2.
struct alignas(8) OutputState {
  uint16_t outputs;
  uint8_t channels;
  uint8_t reserved[29];
};
static_assert(sizeof(OutputState) == 32);
static_assert(offsetof(OutputState, channels) == 2);

uint64_t monotonic_ms() {
  timespec timestamp{};
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0 || timestamp.tv_sec < 0)
    return UINT64_MAX;
  return static_cast<uint64_t>(timestamp.tv_sec) * 1000u +
         static_cast<uint64_t>(timestamp.tv_nsec) / 1000000u;
}

void erase(void *memory, size_t bytes) {
  volatile unsigned char *cursor = static_cast<unsigned char *>(memory);
  while (bytes--) *cursor++ = 0;
}

template <typename Function>
Function resolve(void *module, const char *name) {
  static_assert(sizeof(Function) == sizeof(void *));
  Function function = nullptr;
  void *symbol = dlsym(module, name);
  if (symbol) std::memcpy(&function, &symbol, sizeof(function));
  return function;
}

struct Session {
  int user;
  bool (*active)(int);
  bool (*notice)(const char *);
  void *in_module = nullptr;
  void *out_module = nullptr;
  void *memory = nullptr;
  bool locked = false;
  bool close_failed = false;
  bool output_close_failed = false;
  int input = -1;
  int output = -1;
  unsigned captured = 0;
  unsigned played = 0;
  uint32_t peak = 0;
  uint64_t started = UINT64_MAX;
  uint64_t limit = 0;
  using open_fn = int (*)(int, int, int, uint32_t, uint32_t, uint32_t);
  using input_fn = int (*)(int, void *);
  using handle_fn = int (*)(int);
  using output_state_fn = int (*)(int, OutputState *);
  open_fn open_input = nullptr;
  input_fn read_input = nullptr;
  handle_fn input_state = nullptr;
  handle_fn close_input = nullptr;
  output_state_fn output_state = nullptr;

  int guard() const {
    if (!active(user)) return -ECANCELED;
    const uint64_t now = monotonic_ms();
    if (now == UINT64_MAX || started == UINT64_MAX || now < started)
      return -EIO;
    return now - started >= limit ? -ETIMEDOUT : 0;
  }

  int begin_phase(uint64_t milliseconds) {
    started = monotonic_ms();
    limit = milliseconds;
    return guard();
  }

  int check_capture() const {
    int result = guard();
    if (result < 0) return result;
    const int state = input_state(input);
    result = guard();
    if (state != 0) {
      LOG_WARN("Hyper P10 MIC state rejected code=0x%08x",
               static_cast<unsigned>(state));
      return state < 0 ? state : -EACCES;
    }
    return result;
  }

  int check_route() const {
    int result = guard();
    if (result < 0) return result;
    OutputState state{};
    result = output_state(output, &state);
    if (result < 0) return result;
    result = guard();
    if (result < 0) return result;
    if (state.outputs != 4 || state.channels != 1) {
      LOG_WARN("Hyper P10 OUT route rejected outputs=%u channels=%u",
               static_cast<unsigned>(state.outputs),
               static_cast<unsigned>(state.channels));
      return -ENODEV;
    }
    return 0;
  }

  int send_output(const int16_t *block) const {
    int result = check_route();
    if (result < 0) return result;
    result = sceAudioOutOutput(output, block);
    return result < 0 ? result : guard();
  }

  int shut_input() {
    if (input < 0) return 0;
    const int result = close_input(input);
    input = -1;
    close_failed = result < 0;
    LOG_INFO("Hyper P10 MIC close code=0x%08x",
             static_cast<unsigned>(result));
    return result;
  }

  int prepare() {
    if (!active(user)) return -ECANCELED;
    in_module = dlopen("libSceAudioIn.sprx", RTLD_LAZY | RTLD_LOCAL);
    out_module = dlopen("libSceAudioOut.sprx", RTLD_LAZY | RTLD_LOCAL);
    if (!in_module || !out_module) {
      LOG_WARN("Hyper P10 prepare audio module unavailable");
      return -ENOSYS;
    }
    open_input = resolve<open_fn>(in_module, "sceAudioInOpen");
    read_input = resolve<input_fn>(in_module, "sceAudioInInput");
    input_state = resolve<handle_fn>(in_module, "sceAudioInGetSilentState");
    close_input = resolve<handle_fn>(in_module, "sceAudioInClose");
    output_state = resolve<output_state_fn>(out_module, "sceAudioOutGetPortState");
    if (!open_input || !read_input || !input_state || !close_input ||
        !output_state) {
      LOG_WARN("Hyper P10 prepare audio API unavailable");
      return -ENOSYS;
    }
    memory = mmap(nullptr, kMemoryBytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) {
      memory = nullptr;
      const int failure = errno ? errno : ENOMEM;
      LOG_WARN("Hyper P10 prepare mmap failed errno=%d", failure);
      return -failure;
    }
    if (reinterpret_cast<uintptr_t>(memory) % 256u != 0) return -EINVAL;
#ifdef MADV_NOCORE
    if (madvise(memory, kMemoryBytes, MADV_NOCORE) != 0) {
      const int failure = errno ? errno : EIO;
      LOG_WARN("Hyper P10 prepare no-core protection failed errno=%d", failure);
      return -failure;
    }
#else
    return -ENOTSUP;
#endif
    if (mlock(memory, kMemoryBytes) != 0) {
      const int failure = errno ? errno : EIO;
      LOG_WARN("Hyper P10 prepare memory lock failed errno=%d", failure);
      return -failure;
    }
    locked = true;
    LOG_INFO("Hyper P10 protected audio memory ready bytes=%u",
             static_cast<unsigned>(kMemoryBytes));
    return 0;
  }

  int capture() {
    if (!notice("Preparati: fra 2 secondi apriro il microfono per circa 2 secondi. Cerchio annulla. La voce resta in memoria e viene cancellata."))
      return -EACCES;
    int result = begin_phase(3000);
    if (result < 0) return result;
    for (;;) {
      result = guard();
      if (result < 0) return result;
      const uint64_t now = monotonic_ms();
      if (now == UINT64_MAX || now < started) return -EIO;
      if (now - started >= 2000u) break;
      timespec delay{0, 100000000L};
      while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) return -EIO;
        result = guard();
        if (result < 0) return result;
      }
    }
    if (!notice("PARLA ORA: microfono per circa 2 secondi. Cerchio annulla."))
      return -EACCES;
    result = begin_phase(kCaptureLimitMs);
    if (result < 0) return result;
    input = open_input(user, 1, 0, kInputGrain, 16000, 1);
    LOG_INFO("Hyper P10 MIC open code=0x%08x", static_cast<unsigned>(input));
    if (input < 0) return input;
    auto *pcm = static_cast<int16_t *>(memory);
    for (size_t index = 0; index < kInputBlocks; ++index) {
      result = check_capture();
      if (result < 0) return result;
      int16_t *block = pcm + index * kInputGrain;
      result = read_input(input, block);
      if (result < 0) {
        LOG_WARN("Hyper P10 MIC input block=%u code=0x%08x",
                 static_cast<unsigned>(index), static_cast<unsigned>(result));
        return result;
      }
      result = check_capture();
      if (result < 0) return result;
      ++captured;
      for (size_t frame = 0; frame < kInputGrain; ++frame) {
        const int32_t sample = block[frame];
        const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
        if (magnitude > peak) peak = magnitude;
      }
    }
    return guard();
  }

  int echo() {
    if (!notice("Microfono chiuso. Riproduco la frase dal DualSense; poi la cancello. Cerchio annulla."))
      return -EACCES;
    int result = begin_phase(kPlaybackLimitMs);
    if (result < 0) return result;
    const int init = sceAudioOutInit();
    LOG_INFO("Hyper P10 OUT init code=0x%08x", static_cast<unsigned>(init));
    result = guard();
    if (result < 0) return result;
    output = sceAudioOutOpen(user, 4, 0, kOutputGrain, 48000, 0);
    LOG_INFO("Hyper P10 OUT open code=0x%08x", static_cast<unsigned>(output));
    if (output < 0) return output;
    result = check_route();
    if (result < 0) return result;
    const int32_t volumes[8] = {0x4000, 0x4000, 0x4000, 0x4000,
                                0x4000, 0x4000, 0x4000, 0x4000};
    result = sceAudioOutSetVolume(output, 1, volumes);
    if (result < 0) return result;
    result = guard();
    if (result < 0) return result;
    const auto *pcm = static_cast<const int16_t *>(memory);
    auto *block = static_cast<int16_t *>(memory) + kInputSamples;
    size_t source = 0;
    unsigned repeat = 0;
    while (source < kInputSamples) {
      for (size_t frame = 0; frame < kOutputGrain; ++frame) {
        block[frame] = static_cast<int16_t>(pcm[source] / 2);
        if (++repeat == 3) { repeat = 0; ++source; }
      }
      result = send_output(block);
      if (result < 0) return result;
      ++played;
    }
    erase(block, kOutputGrain * sizeof(*block));
    result = send_output(block);
    if (result < 0) return result;
    return send_output(nullptr);
  }

  int cleanup(int result) {
    const int input_closed = shut_input();
    if (input_closed < 0) result = input_closed;
    if (output >= 0) {
      const int closed = sceAudioOutClose(output);
      output = -1;
      output_close_failed = closed < 0;
      LOG_INFO("Hyper P10 OUT close code=0x%08x", static_cast<unsigned>(closed));
      if (closed < 0) result = closed;
    }
    if (memory) {
      erase(memory, kMemoryBytes);
      if (locked) (void)munlock(memory, kMemoryBytes);
      (void)munmap(memory, kMemoryBytes);
      memory = nullptr;
    }
    if (in_module) (void)dlclose(in_module);
    if (out_module) (void)dlclose(out_module);
    return result;
  }
};
} // namespace

void hyper_p10_voice_echo(int user, bool (*session_active)(int),
                          bool (*notice)(const char *)) {
  if (user <= 0 || user == 0xfe || user == 0xff || !session_active || !notice)
    return;
  Session session{user, session_active, notice};
  int result = session.prepare();
  if (result == 0) result = session.capture();
  const int closed = session.shut_input();
  if (closed < 0) result = closed;
  LOG_INFO("Hyper P10 capture code=0x%08x blocks=%u peak=%u",
           static_cast<unsigned>(result), session.captured, session.peak);
  if (result == 0 && session.peak < 64) result = -ENOMSG;
  if (result == 0) result = session.echo();
  result = session.cleanup(result);
  LOG_INFO("Hyper P10 end code=0x%08x captured_blocks=%u output_blocks=%u pcm_erased=1",
           static_cast<unsigned>(result), session.captured, session.played);
  if (session.close_failed)
    (void)notice("Chiusura microfono non confermata. Voce cancellata; riavvia la console.");
  else if (session.output_close_failed)
    (void)notice("Chiusura altoparlante non confermata. Voce cancellata; riavvia la console.");
  else if (result == 0)
    (void)notice("Prova voce completata. Audio cancellato dalla memoria.");
  else
    (void)notice("Prova voce interrotta o ingresso silenzioso. Audio cancellato dalla memoria.");
}
