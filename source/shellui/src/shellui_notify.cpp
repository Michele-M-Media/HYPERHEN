/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ShellUI notify(const char *, ...) — no watermark bool (historical UI API).
 */

#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include <onion/notify.h>
#include <cstdarg>
#include <cstdio>

void notify(const char *text, ...) {
  va_list args{};
  va_start(args, text);
  // show_watermark ignored by format; still prefixes [HyperHEN] like daemons.
  // Send goes through onion_notify_set_send trampoline (dlsym pointer safe).
  onion_notify_v(/*show_watermark=*/0, text, args);
  va_end(args);
  LOG_DEBUG("Notify send returned (onion_notify path)");
}
