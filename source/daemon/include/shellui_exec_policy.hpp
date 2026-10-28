/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <string_view>
#include <sys/types.h>

namespace onion::daemon {

/* An app title identifies a process group, not the ShellUI executable.
 * In particular, NKWebProcess can inherit NPXS40087 from its owner.
 * Keep this filter ahead of any ShellUI lifecycle side effects.
 */
struct ShellUiExecPolicy final {
  template <typename ResolvePid>
  static bool accepts(pid_t event_pid, int name_rc,
                      std::string_view process_name, int app_info_rc,
                      std::string_view title_id,
                      ResolvePid resolve_shellui_pid) noexcept {
    if (event_pid <= 1)
      return false;

    const bool name_known = name_rc == 0;
    if (name_known) {
      if (process_name != "SceShellUI")
        return false;
    } else if (app_info_rc != 0 || title_id != "NPXS40087") {
      return false;
    }

    const pid_t live_pid = resolve_shellui_pid();
    if (live_pid > 1)
      return live_pid == event_pid;

    /* A title alone must never invalidate an existing Toolbox session.
     * The exact executable name still identifies an early real EXEC when
     * the process-list resolver has not caught up yet.
     */
    return name_known;
  }
};

} // namespace onion::daemon
