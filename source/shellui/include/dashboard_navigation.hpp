/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstdint>
#include <string_view>

namespace dashboard_navigation {

enum class Destination { Original, Toolbox, HyperShell };

// Every HomeUI profile emits the NavUI URI for the HyperHEN dashboard icon.
// Keep that shared Dashboard action bound to the Toolbox/Debug menu. HyperShell
// has its own explicit URI and controller shortcut.
constexpr Destination resolve(std::string_view uri, bool hypershell_enabled) {
  if (uri == "HyperHEN?NavUI" || uri == "HyperHEN?NavUI=1")
    return Destination::Toolbox;

  if (uri == "HyperHEN?HyperShell" || uri == "HyperHEN?HyperShell=1")
    return hypershell_enabled ? Destination::HyperShell : Destination::Toolbox;

  return Destination::Original;
}

// Trigger when the full chord first becomes complete.  GamePad::Buttons can
// carry non-chord state bits (for example Intercepted), so requiring exact
// equality makes otherwise valid shortcuts disappear on newer firmware.
constexpr bool hypershell_shortcut_pressed(std::uint32_t buttons,
                                           std::uint32_t previous,
                                           std::uint32_t shortcut_mask,
                                           bool enabled) {
  return enabled && shortcut_mask != 0 &&
         (buttons & shortcut_mask) == shortcut_mask &&
         (previous & shortcut_mask) != shortcut_mask;
}

} // namespace dashboard_navigation
