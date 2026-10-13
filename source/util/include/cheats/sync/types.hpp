#pragma once

#include <cstddef>

namespace onion::cheats::sync {

/** User-facing [cheats] mirror= token. */
enum class CheatMirrorPref : int {
  Auto = 0,
  Github = 1,
  Cnb = 2,
};

/** Concrete archive mirror. Catalog::slugFor() is keyed by this. */
enum class CheatMirrorId : int {
  Github = 1,
  Cnb = 2,
};

/** Narrow result codes shared by HTTPS, ZIP extraction, and sync. */
enum class SyncStatus : int {
  Ok = 0,
  Network = -1,
  Io = -2,
  Protocol = -3,
  Unavailable = -4,
  NoSpace = -5,
  Busy = -6,
  Rejected = -7,
  Tls = -8,
  Clock = -9,
  Cancelled = -10,
};

inline bool is_source_failure(SyncStatus s) {
  return s == SyncStatus::Network || s == SyncStatus::Protocol ||
         s == SyncStatus::Tls || s == SyncStatus::Clock;
}

inline const char *sync_status_name(SyncStatus s) {
  switch (s) {
  case SyncStatus::Ok:
    return "ok";
  case SyncStatus::Network:
    return "network";
  case SyncStatus::Io:
    return "io";
  case SyncStatus::Protocol:
    return "protocol";
  case SyncStatus::Unavailable:
    return "unavailable";
  case SyncStatus::NoSpace:
    return "no_space";
  case SyncStatus::Busy:
    return "busy";
  case SyncStatus::Rejected:
    return "rejected";
  case SyncStatus::Tls:
    return "tls";
  case SyncStatus::Clock:
    return "clock";
  case SyncStatus::Cancelled:
    return "cancelled";
  }
  return "unknown";
}

using SyncProgressFn = void (*)(const char *phase, size_t completed,
                                size_t total, void *user);
using SyncCancelFn = bool (*)(void *user);

} // namespace onion::cheats::sync
