#pragma once

#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/i_cheat_mirror.hpp"
#include "cheats/sync/i_http_transport.hpp"
#include "cheats/sync/types.hpp"

#include <string>

namespace onion::cheats::sync {

/** Download one ZIP, extract only catalog roots, install, then clean up. */
class CheatSyncEngine {
public:
  using InstallProgressFn = void (*)(size_t completed, size_t total,
                                     void *user);
  using FlattenFn = SyncStatus (*)(const char *root,
                                   InstallProgressFn progress,
                                   void *progress_user,
                                   SyncCancelFn should_cancel,
                                   void *cancel_user);

  struct Result {
    SyncStatus status = SyncStatus::Rejected;
    CheatMirrorId used_mirror = CheatMirrorId::Github;
    std::string url;
    std::string error;
  };

  CheatSyncEngine(IHttpTransport &http, FlattenFn flatten);

  void setProgressHandler(SyncProgressFn fn, void *user);
  void setCancelHandler(SyncCancelFn fn, void *user);

  Result run(const ICheatCatalog &catalog, const ICheatMirror &primary,
             const ICheatMirror *fallback, const char *data_root);

private:
  SyncStatus tryOne(const ICheatCatalog &catalog, const ICheatMirror &mirror,
                    const char *data_root, Result &out);

  IHttpTransport &http_;
  FlattenFn flatten_ = nullptr;
  SyncProgressFn progress_ = nullptr;
  void *progress_user_ = nullptr;
  SyncCancelFn should_cancel_ = nullptr;
  void *cancel_user_ = nullptr;
};

} // namespace onion::cheats::sync
