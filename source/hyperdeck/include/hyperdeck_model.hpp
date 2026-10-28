/* Copyright (C) 2026 HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <onion/boot_mode_5xx.h>
#include <onion/engine_selector.h>
#include <onion/firmware_capabilities.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace hyperdeck {

enum class State { Unknown, Ready, Waiting, Offline, Failed, NotUsed };
enum class Tab { Home, Reactor, Quick, Appearance };
enum class Theme { HyperGlass, Reactor, Blackout };
enum class Action { None, Payloads, Plugins, Cheats, Quick, Reactor, Toolbox,
                    Klog, Ftp, ElfLoader, Overlay, Linux, ThemeGlass,
                    ThemeReactor, ThemeBlackout, Motion, Refresh, Back };

struct Stage {
  const char *name = "";
  const char *detail = "";
  State state = State::Unknown;
};

struct Snapshot {
  uint32_t firmware = 0;
  std::string version = "";
  std::string ip = "--";
  std::string cpu = "--";
  std::string soc = "--";
  std::string fan = "--";
  std::array<Stage, 5> stages{};
  size_t stage_count = 0;
  std::array<bool, 3> tools{};
  Onion5xxBootMode boot_mode_5xx = ONION_5XX_BOOT_MODE_DEFAULT;
  bool overlay = false;
  bool fresh = false;
  bool linux_supported = false;
  uint64_t revision = 0;
  uint64_t sampled_ms = 0;
};

inline const char *state_text(State state) {
  switch (state) {
  case State::Ready: return "READY";
  case State::Waiting: return "WAITING";
  case State::Offline: return "OFFLINE";
  case State::Failed: return "FAILED";
  case State::NotUsed: return "NOT USED";
  default: return "UNKNOWN";
  }
}

inline std::string firmware_text(uint32_t fw) {
  char value[20];
  // Sony's public software fields use packed decimal (0x1342 == 13.42).
  std::snprintf(value, sizeof(value), "%x.%02x", (fw >> 24) & 0xff,
                (fw >> 16) & 0xff);
  return value;
}

inline const char *route_text(uint32_t firmware, Onion5xxBootMode boot_mode_5xx = ONION_5XX_BOOT_MODE_DEFAULT) {
  switch (onion_firmware_route(firmware)) {
  case ONION_FIRMWARE_ROUTE_LEGACY_HEN:
    return onion_engine_selector_accepts(firmware,boot_mode_5xx) ?
           boot_mode_5xx == ONION_5XX_BOOT_MODE_PS5HEN ? "PS5 HEN" :
           "HYPERCORE / COMPATIBILITY" : "LEGACY HEN";
  case ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX:
    return boot_mode_5xx == ONION_5XX_BOOT_MODE_BYPERVISOR ? "BYEPERVISOR / 5.xx" :
           boot_mode_5xx == ONION_5XX_BOOT_MODE_HYPERCORE ? "HYPERCORE / A53 PPR" :
           "5.xx / MODE UNKNOWN";
  case ONION_FIRMWARE_ROUTE_A53_PPR: return "A53 / PPR";
  case ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR: return "HYPERCORE / NO PPR";
  case ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX: return "BYEPERVISOR / RESUME";
  case ONION_FIRMWARE_ROUTE_HYPERVISOR_940: return "BYEPERVISOR / 9.40 RESUME";
  case ONION_FIRMWARE_ROUTE_KSTUFF_LITE: return "KSTUFF-LITE";
  default: return "NO EXACT PROFILE";
  }
}

inline void describe_stages(Snapshot &s) {
  const auto route = onion_firmware_route(s.firmware);
  s.stage_count = 0;
  const auto append = [&](const char *name, const char *detail) {
    s.stages[s.stage_count++] = {name, detail, State::Unknown};
  };
  if (route == ONION_FIRMWARE_ROUTE_UNSUPPORTED)
    return;
  if (onion_engine_selector_supported(s.firmware) &&
      onion_engine_selector_accepts(s.firmware,s.boot_mode_5xx)) {
    if (s.boot_mode_5xx == ONION_5XX_BOOT_MODE_PS5HEN)
      append("PS5 HEN", "Integrated PS5 HEN completion marker");
    else if (s.boot_mode_5xx == ONION_5XX_BOOT_MODE_BYPERVISOR)
      append("BYEPERVISOR", "5.xx direct hypervisor engine");
    else
      append("HYPERCORE", onion_engine_selector_is_legacy(s.firmware) ?
             "Available compatibility path; core marker" : "A53 / PPR game path; core marker");
    append("AUTOMOUNT", "Mount service process");
    append("DASHBOARD", "HyperHEN dashboard and Toolbox");
    append("HYPERDECK", "Media extension process");
    return;
  }
  if (route == ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX ||
      route == ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX ||
      route == ONION_FIRMWARE_ROUTE_HYPERVISOR_940)
    append("BYEPERVISOR", "Completion marker");
  append(route == ONION_FIRMWARE_ROUTE_LEGACY_HEN ? "LEGACY HEN" : "HYPERCORE",
         route == ONION_FIRMWARE_ROUTE_A53_PPR ? "A53 / PPR route; core marker" :
         route == ONION_FIRMWARE_ROUTE_KSTUFF_LITE ? "kstuff-lite core marker" :
         route == ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR ? "Compatibility route; PPR disabled" :
         "Core completion marker");
  append("AUTOMOUNT", "Mount service process");
  const uint32_t caps = onion_firmware_capabilities(route);
  if (caps & ONION_FIRMWARE_CAP_DAEMON)
    append("DAEMON", "Runtime service process");
  if (caps & ONION_FIRMWARE_CAP_TOOLBOX)
    append("HYPERUI", "Classic Toolbox process");
}

// Failure evidence always wins. A missing sample must never become READY.
constexpr State observed_state(bool ready, bool failed, bool starting = false) {
  return failed ? State::Failed : ready ? State::Ready :
         starting ? State::Waiting : State::Unknown;
}

inline State overall_state(const Snapshot &s, uint64_t now) {
  if (!s.fresh || now < s.sampled_ms || now - s.sampled_ms > 3500 || !s.stage_count)
    return State::Unknown;
  bool complete = true;
  for (size_t i = 0; i < s.stage_count; ++i) {
    if (s.stages[i].state == State::Failed)
      return State::Failed;
    complete &= s.stages[i].state == State::Ready;
  }
  return complete ? State::Ready : State::Waiting;
}

inline int item_count(Tab tab) {
  return tab == Tab::Home || tab == Tab::Quick ? 6 : tab == Tab::Appearance ? 4 : 1;
}

inline Action action_at(Tab tab, int selection) {
  constexpr Action home[] = {Action::Payloads, Action::Plugins, Action::Cheats,
                             Action::Quick, Action::Reactor, Action::Toolbox};
  constexpr Action quick[] = {Action::Klog, Action::Ftp, Action::ElfLoader,
                              Action::Overlay, Action::Linux, Action::Toolbox};
  constexpr Action appearance[] = {Action::ThemeGlass, Action::ThemeReactor,
                                   Action::ThemeBlackout, Action::Motion};
  if (selection < 0 || selection >= item_count(tab)) return Action::None;
  return tab == Tab::Home ? home[selection] : tab == Tab::Quick ? quick[selection] :
         tab == Tab::Appearance ? appearance[selection] : Action::Refresh;
}

struct Navigation {
  Tab tab = Tab::Home;
  int selected = 0;
  Theme theme = Theme::HyperGlass;
  bool reduced_motion = false;
  uint32_t held = 0;
  uint64_t repeat_at = 0;

  void switch_tab(int delta) {
    tab = static_cast<Tab>((static_cast<int>(tab) + delta + 4) % 4);
    selected = 0;
  }

  // Pure input policy. Managed calls and service requests belong to the UI tick.
  Action input(uint32_t buttons, uint64_t now) {
    const uint32_t down = buttons & ~held;
    const bool repeat = (buttons & 15) && buttons == held && now >= repeat_at;
    held = buttons;
    if (down & 64) return Action::Back;
    if (down & 1024) { switch_tab(-1); return Action::None; }
    if (down & 2048) { switch_tab(1); return Action::None; }
    if ((down & 15) || repeat) {
      int delta = 0;
      if (tab == Tab::Home) {
        if (buttons & 1) delta = (selected % 3) ? -1 : 0;
        else if (buttons & 4) delta = (selected % 3 < 2) ? 1 : 0;
        else if (buttons & 2) delta = selected >= 3 ? -3 : 0;
        else if (buttons & 8) delta = selected < 3 ? 3 : 0;
      } else {
        if (buttons & (1 | 2)) delta = -1;
        else if (buttons & (4 | 8)) delta = 1;
      }
      selected = std::clamp(selected + delta, 0, item_count(tab) - 1);
      repeat_at = now + (repeat ? 100 : 350);
    }
    return down & 128 ? action_at(tab, selected) : Action::None;
  }
};

inline float reveal_offset(uint64_t elapsed, bool reduced_motion) {
  if (reduced_motion || elapsed >= 240) return 0;
  const float remaining = 1.0f - static_cast<float>(elapsed) / 240.0f;
  return 28.0f * remaining * remaining * remaining;
}

} // namespace hyperdeck
