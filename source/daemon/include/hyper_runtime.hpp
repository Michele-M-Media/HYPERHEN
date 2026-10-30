#pragma once

#include <string>
#include <sys/types.h>
#include <onion/hyper_runtime_selection.hpp>

namespace onion::daemon::hyper {
// Settings changes apply immediately; the owner thread serializes all spawn
// and stop operations. Only a validated Toolbox-ready event arms startup.
void set_enabled(bool enabled);
bool set_selection(int model_id, int voice_id);
onion::hyper::RuntimeSelection selection();
void toolbox_ready(pid_t pid);
void toolbox_lost();
bool start();
void stop();
// Cached only: no network, hashing, or filesystem work from the IPC handler.
std::string status_json();
} // namespace onion::daemon::hyper
