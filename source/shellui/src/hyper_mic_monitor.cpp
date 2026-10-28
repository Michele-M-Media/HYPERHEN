#include "hyper_mic_monitor.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <time.h>

#include <onion/log.h>

namespace {
uint64_t monotonic_ms() {
  timespec timestamp{};
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0 || timestamp.tv_sec < 0)
    return UINT64_MAX;
  return static_cast<uint64_t>(timestamp.tv_sec) * 1000u +
         static_cast<uint64_t>(timestamp.tv_nsec) / 1000000u;
}

bool pause_sample() {
  timespec delay{0, 100000000L};
  while (nanosleep(&delay, &delay) != 0) {
    if (errno != EINTR) return false;
  }
  return true;
}

template <typename Function>
Function resolve(void *module, const char *name) {
  static_assert(sizeof(Function) == sizeof(void *));
  Function function = nullptr;
  void *symbol = dlsym(module, name);
  if (symbol) std::memcpy(&function, &symbol, sizeof(function));
  return function;
}
} // namespace

void hyper_p9_monitor_mute(int user, bool (*session_active)(int),
                           bool (*notice)(const char *)) {
  if (!session_active || !notice || !session_active(user)) return;
  void *module = dlopen("libSceAudioIn.sprx", RTLD_LAZY | RTLD_LOCAL);
  if (!module) {
    LOG_WARN("Hyper P9 MIC AudioIn module unavailable");
    (void)notice("Test Mute non disponibile. Ingresso non aperto.");
    return;
  }
  using open_fn = int (*)(int, int, int, uint32_t, uint32_t, uint32_t);
  using state_fn = int (*)(int);
  using close_fn = int (*)(int);
  const auto open_port = resolve<open_fn>(module, "sceAudioInOpen");
  const auto get_state = resolve<state_fn>(module, "sceAudioInGetSilentState");
  const auto close_port = resolve<close_fn>(module, "sceAudioInClose");
  if (!open_port || !get_state || !close_port) {
    LOG_WARN("Hyper P9 MIC mute API unavailable; input not opened");
    (void)dlclose(module);
    (void)notice("Test Mute non disponibile. Ingresso non aperto.");
    return;
  }
  if (!notice("Test Mute: 12 secondi. Premi Mute, attendi e ripremilo. Cerchio annulla. Nessuna registrazione.") ||
      !session_active(user)) {
    LOG_WARN("Hyper P9 MIC notice failed or session cancelled; input not opened");
    (void)dlclose(module);
    return;
  }
  const int handle = open_port(user, 1, 0, 256, 16000, 1);
  LOG_INFO("Hyper P9 MIC open code=0x%08x", static_cast<unsigned>(handle));
  if (handle < 0) {
    (void)dlclose(module);
    (void)notice("Apertura ingresso audio non riuscita.");
    return;
  }

  const uint64_t started = monotonic_ms();
  unsigned samples = 0;
  unsigned transitions = 0;
  int prior = 0;
  bool have_state = false;
  const char *reason = "completed";
  for (;;) {
    const uint64_t now = monotonic_ms();
    if (started == UINT64_MAX || now == UINT64_MAX || now < started) {
      reason = "clock_error";
      break;
    }
    if (now - started >= 12000u) break;
    if (!session_active(user)) {
      reason = "cancelled_or_input_stale";
      break;
    }
    const int state = get_state(handle);
    if (state < 0) {
      LOG_WARN("Hyper P9 MIC mute state failed code=0x%08x",
               static_cast<unsigned>(state));
      reason = "state_api_error";
      break;
    }
    ++samples;
    if (!have_state || state != prior) {
      if (have_state) ++transitions;
      LOG_INFO("Hyper P9 MIC raw_mute_state=%d elapsed_ms=%llu", state,
               static_cast<unsigned long long>(now - started));
      prior = state;
      have_state = true;
    }
    if (!pause_sample()) {
      reason = "sleep_error";
      break;
    }
  }
  const int closed = close_port(handle);
  LOG_INFO("Hyper P9 MIC close code=0x%08x", static_cast<unsigned>(closed));
  (void)dlclose(module);
  LOG_INFO("Hyper P9 MIC monitor end reason=%s samples=%u transitions=%u no_PCM=1",
           reason, samples, transitions);
  if (closed < 0)
    (void)notice("Chiusura ingresso audio non confermata. Riavvia la console.");
  else
    (void)notice("Test Mute terminato. Ingresso audio chiuso.");
}
