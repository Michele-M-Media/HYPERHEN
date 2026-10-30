#pragma once
#include <onion/hyper_assets_manifest.hpp>
#include "cheats/sync/i_http_transport.hpp"
#include <cstddef>
#include <cstdint>

namespace onion::hyper {
using InstallerProgress = void (*)(const char *, uint64_t, uint64_t, void *);
using InstallerCancel = bool (*)(void *);
using InstallerQuiesce = onion::cheats::sync::SyncStatus (*)(void *);
struct InstallInputs {
  const char *root;
  const ModelFile *models;
  size_t model_count;
  const AssetFile *voices;
  size_t voice_count;
  const unsigned char *voice_zip;
  size_t voice_zip_size;
  const char *voice_zip_sha256;
  onion::cheats::sync::IHttpTransport *http;
  InstallerProgress progress = nullptr;
  InstallerCancel cancelled = nullptr;
  void *user = nullptr;
  // Invoked after .installing becomes visible, before modifying asset files.
  InstallerQuiesce quiesce = nullptr;
  bool allow_download = true;
  // Scoped readiness prevents failed optional downloads from disabling a
  // previously verified model. The legacy defaults preserve R34 tests/callers.
  const char *ready_name = "/.assets-ready";
  const char *ready_version = kAssetVersion;
  const char *base_ready_name = nullptr;
  const char *base_ready_version = nullptr;
  const char *compat_ready_name = nullptr;
  size_t common_model_count = 0;
};
// Single synchronous transaction. Caller owns job serialization.
onion::cheats::sync::SyncStatus install_assets(const InstallInputs &inputs);
} // namespace onion::hyper
