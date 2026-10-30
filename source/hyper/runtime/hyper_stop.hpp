#pragma once

#include <csignal>

// The handler only writes sig_atomic_t. Normal worker code performs every
// cancellation, destructor, socket close and model release.
inline volatile sig_atomic_t hyper_stop_flag = 0;
inline void hyper_stop_signal(int) { hyper_stop_flag = 1; }
inline bool hyper_stop_requested() noexcept { return hyper_stop_flag != 0; }
inline bool hyper_install_stop_handler() {
  struct sigaction action {};
  action.sa_handler = hyper_stop_signal;
  sigemptyset(&action.sa_mask);
  return sigaction(SIGTERM, &action, nullptr) == 0;
}
