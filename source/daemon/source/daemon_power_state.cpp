/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "daemon_power_state.hpp"
#include <onion/platform.h>
#include <cstdint>
#include <mutex>

extern "C" {
int sceKernelOpenEventFlag(intptr_t *event_flag, const char *name);
int sceKernelPollEventFlag(intptr_t event_flag, uint64_t bits,
                           unsigned int wait_mode, uint64_t *result);
int sceKernelCloseEventFlag(intptr_t event_flag);
}

namespace {

constexpr unsigned int kWaitModeOr = 2;
constexpr unsigned int kStateMask = 0xffff;
constexpr unsigned int kStateSuspendOnGoing = 300;
constexpr unsigned int kStateMainOnStandby = 500;
constexpr unsigned int kStateWorking = 1000;

std::mutex g_power_state_mutex;
intptr_t g_state_event_flag = -1;
int g_last_open_error = 0;
DaemonPowerState g_last_reported_state = DaemonPowerState::Unknown;

const char *state_name(DaemonPowerState state) {
  switch (state) {
  case DaemonPowerState::Working:
    return "WORKING";
  case DaemonPowerState::MainOnStandby:
    return "MAIN_ON_STANDBY";
  case DaemonPowerState::SuspendOnGoing:
    return "SUSPEND_ON_GOING";
  case DaemonPowerState::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

bool ensure_event_flag_locked() {
  if (g_state_event_flag >= 0) {
    return true;
  }

  const int rc = sceKernelOpenEventFlag(&g_state_event_flag,
                                        "SceSystemStateMgrInfo");
  if (rc < 0) {
    g_state_event_flag = -1;
    if (rc != g_last_open_error) {
      LOG_DEBUG("rest: power state event flag unavailable rc=0x%x", rc);
      g_last_open_error = rc;
    }
    return false;
  }
  g_last_open_error = 0;
  LOG_DEBUG("rest: power state event flag opened handle=%lld",
            static_cast<long long>(g_state_event_flag));
  return true;
}

DaemonPowerState read_state_locked() {
  if (!ensure_event_flag_locked()) {
    return DaemonPowerState::Unknown;
  }

  uint64_t pattern = 0;
  const int rc = sceKernelPollEventFlag(g_state_event_flag, UINT64_MAX,
                                        kWaitModeOr, &pattern);
  if (rc < 0) {
    LOG_DEBUG("rest: power state event flag poll failed rc=0x%x", rc);
    (void)sceKernelCloseEventFlag(g_state_event_flag);
    g_state_event_flag = -1;
    return DaemonPowerState::Unknown;
  }

  switch (static_cast<unsigned int>(pattern & kStateMask)) {
  case kStateWorking:
    return DaemonPowerState::Working;
  case kStateMainOnStandby:
    return DaemonPowerState::MainOnStandby;
  case kStateSuspendOnGoing:
    return DaemonPowerState::SuspendOnGoing;
  default:
    return DaemonPowerState::Unknown;
  }
}

} // namespace

DaemonPowerState daemon_power_state_get() {
  std::lock_guard<std::mutex> lock(g_power_state_mutex);
  const DaemonPowerState state = read_state_locked();
  if (state != g_last_reported_state) {
    LOG_INFO("rest: power state -> %s", state_name(state));
    g_last_reported_state = state;
  }
  return state;
}

bool daemon_power_state_is_sleeping(DaemonPowerState state) {
  return state == DaemonPowerState::MainOnStandby ||
         state == DaemonPowerState::SuspendOnGoing;
}

bool daemon_power_state_is_sleeping() {
  return daemon_power_state_is_sleeping(daemon_power_state_get());
}
