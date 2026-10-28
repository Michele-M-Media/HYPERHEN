/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */

#include "../source/shellui/include/dashboard_navigation.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

using dashboard_navigation::Destination;
using dashboard_navigation::hypershell_shortcut_pressed;
using dashboard_navigation::resolve;

constexpr std::uint32_t kTriangle = 32U;
constexpr std::uint32_t kOptions = 256U;
constexpr std::uint32_t kShare = 512U;
constexpr std::uint32_t kL1 = 1024U;
constexpr std::uint32_t kR1 = 2048U;
constexpr std::uint32_t kL2 = 4096U;
constexpr std::uint32_t kL3 = 16384U;
constexpr std::uint32_t kR3 = 32768U;
constexpr std::uint32_t kShortcut = kL1 | kR1 | kTriangle;

static_assert(resolve("HyperHEN?NavUI=1", true) == Destination::Toolbox);
static_assert(resolve("HyperHEN?HyperShell=1", false) == Destination::Toolbox);
static_assert(hypershell_shortcut_pressed(kShortcut, 0, kShortcut, true));
static_assert(!hypershell_shortcut_pressed(kShortcut, kShortcut, kShortcut, true));

int checks = 0;
int failures = 0;

void check(bool passed, std::string_view label) {
  ++checks;
  if (!passed) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}

void check_route(std::string_view uri, bool enabled, Destination expected) {
  ++checks;
  if (resolve(uri, enabled) != expected) {
    ++failures;
    std::cerr << "FAIL: URI [" << uri << "] with HyperShell "
              << (enabled ? "enabled" : "disabled") << '\n';
  }
}

void check_routes() {
  constexpr std::string_view toolbox_uris[] = {
      "HyperHEN?NavUI", "HyperHEN?NavUI=1"};
  constexpr std::string_view hypershell_uris[] = {
      "HyperHEN?HyperShell", "HyperHEN?HyperShell=1"};
  constexpr std::string_view other_uris[] = {
      "",
      "HyperHEN",
      "HyperHEN?Cheats",
      "HyperHEN?NavUI=0",
      "HyperHEN?NavUI=10",
      "HyperHEN?NavUI=1&other=1",
      "HyperHEN?NavUI=1/",
      "HyperHEN?navui=1",
      "hyperhen?NavUI=1",
      " HyperHEN?NavUI=1",
      "HyperHEN?NavUI=1 ",
      "HyperHEN?HyperShell=0",
      "HyperHEN?HyperShell=10",
      "HyperHEN?HyperShell=1&other=1",
      "HyperHEN?HyperShell=1/",
      "HyperHEN?Hypershell=1",
      " HyperHEN?HyperShell=1",
      "HyperHEN?HyperShell=1 ",
      "HyperHEN?HyperShell%3D1"};

  for (bool enabled : {false, true}) {
    for (std::string_view uri : toolbox_uris)
      check_route(uri, enabled, Destination::Toolbox);
    for (std::string_view uri : hypershell_uris)
      check_route(uri, enabled,
                  enabled ? Destination::HyperShell : Destination::Toolbox);
    for (std::string_view uri : other_uris)
      check_route(uri, enabled, Destination::Original);
  }
}

void check_shortcut() {
  // A complete press, sustained hold, release, and second press form two opens.
  constexpr std::uint32_t frames[] = {
      0, kL1, kL1 | kR1, kShortcut, kShortcut, 0, kShortcut, kShortcut};
  constexpr bool expected[] = {false, false, false, true,
                               false, false, true, false};
  std::uint32_t previous = 0;
  for (unsigned int i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
    check(hypershell_shortcut_pressed(frames[i], previous, kShortcut, true) ==
              expected[i],
          "shortcut press/hold/release sequence");
    previous = frames[i];
  }

  check(hypershell_shortcut_pressed(kShortcut, kTriangle, kShortcut, true),
        "completing the chord from Triangle");
  check(!hypershell_shortcut_pressed(kShortcut, 0, kShortcut, false),
        "shortcut disabled with the feature");
  check(!hypershell_shortcut_pressed(0, 0, 0, true),
        "empty shortcut mask never activates");

  constexpr std::uint32_t existing_shortcuts[] = {
      kL3 | kR3, kL2 | kTriangle, kOptions, kL2 | kR3, kShare};
  for (std::uint32_t buttons : existing_shortcuts)
    check(!hypershell_shortcut_pressed(buttons, 0, kShortcut, true),
          "existing shortcut retains its own chord");

  constexpr std::uint32_t conflicting_buttons[] = {
      kL2, kL3, kR3, kOptions, kShare};
  for (std::uint32_t extra : conflicting_buttons) {
    check(!hypershell_shortcut_pressed(kShortcut | extra, 0, kShortcut, true),
          "extra conflicting button rejects HyperShell chord");
    check(!hypershell_shortcut_pressed(kShortcut, kShortcut | extra,
                                      kShortcut, true),
          "releasing an extra button does not create a new press");
  }
}

} // namespace

int main() {
  check_routes();
  check_shortcut();
  if (failures != 0) {
    std::cerr << failures << " of " << checks << " dashboard navigation checks failed\n";
    return 1;
  }
  std::cout << "PASS: " << checks << " dashboard navigation checks\n";
  return 0;
}
