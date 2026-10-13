/* Copyright (C) 2026 OnionHEN */

#include "hook_lifecycle.hpp"

#include <atomic>
#include <cstdint>

namespace {

enum class HookState : uint8_t {
  Installing = 0,
  Ready = 1,
  Failed = 2,
};

std::atomic<uint8_t> g_hook_state{static_cast<uint8_t>(HookState::Failed)};

} // namespace

extern "C" void shellui_hooks_begin_install(void) {
  g_hook_state.store(static_cast<uint8_t>(HookState::Installing),
                     std::memory_order_release);
}

extern "C" void shellui_hooks_publish_ready(void) {
  g_hook_state.store(static_cast<uint8_t>(HookState::Ready),
                     std::memory_order_release);
}

extern "C" void shellui_hooks_publish_failed(void) {
  g_hook_state.store(static_cast<uint8_t>(HookState::Failed),
                     std::memory_order_release);
}

extern "C" bool shellui_hooks_are_ready(void) {
  return g_hook_state.load(std::memory_order_acquire) ==
         static_cast<uint8_t>(HookState::Ready);
}
