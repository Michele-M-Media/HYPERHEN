#pragma once
#include <string>
namespace onion::hyper {
// All calls are nonblocking; a single detached worker owns filesystem/network.
bool start_install(int model_id = 0);
bool start_verify_existing(int model_id = 0);
std::string install_status_json();
std::string catalog_status_json();
bool cancel_install();

enum class InstallRecoveryResult {
  NoMarker,
  Recovered,
  InstallerBusy,
  WorkerBusy,
  Unsafe,
  Io,
};
// Local recovery only. A known abandoned .installing marker is removed while
// holding both locks; models, voices and readiness markers are never changed.
// Optional paths permit real filesystem/lock tests without touching the console.
InstallRecoveryResult recover_interrupted_install(
    const char *root = "/data/Hyper",
    const char *worker_lock = "/system_tmp/hyper_audio_probe_p1.lock");
} // namespace onion::hyper
