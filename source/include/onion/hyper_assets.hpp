#pragma once
#include <onion/hyper_assets_manifest.hpp>
#include <onion/hyper_model_catalog.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace onion::hyper {
inline constexpr char kAssetRoot[] = "/data/Hyper";
inline constexpr char kInstallingName[] = "/.installing";
inline constexpr char kReadyName[] = "/.assets-ready";
inline constexpr char kBaseReadyName[] = "/.base-ready";
inline constexpr char kBaseReadyVersion[] = "hyper-base-r35-v1\n";
inline constexpr const ModelFile &kCommonWhisperModel = kModels[1];

inline bool asset_regular_size(const std::string &path, uint64_t expected) {
  struct stat st {};
  return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
         st.st_size >= 0 && static_cast<uint64_t>(st.st_size) == expected;
}
inline bool install_in_progress(const char *root = kAssetRoot) {
  struct stat st {};
  return lstat((std::string(root) + kInstallingName).c_str(), &st) == 0;
}
inline bool base_assets_present(const char *root = kAssetRoot) {
  if (!root) return false;
  const std::string base(root);
  if (!asset_regular_size(base + "/models/" + kCommonWhisperModel.file.path,
                          kCommonWhisperModel.file.size)) return false;
  for (const auto &v : kVoiceFiles)
    if (!asset_regular_size(base + "/espeak-ng-data/" + v.path, v.size)) return false;
  return true;
}
// Cheap lifecycle gate: full hashes belong to the installer worker, never UI.
// Only its durable ready marker permits starting the assistant.
inline bool assets_marker_ready(const char *root = kAssetRoot) {
  if (!root || install_in_progress(root)) return false;
  const std::string base(root);
  const std::string marker = base + kReadyName;
  if (!asset_regular_size(marker, sizeof(kAssetVersion) - 1)) return false;
  FILE *f = std::fopen(marker.c_str(), "rb");
  if (!f) return false;
  char data[sizeof(kAssetVersion)] {};
  const bool marker_ok = std::fread(data, 1, sizeof(kAssetVersion) - 1, f) ==
                            sizeof(kAssetVersion) - 1 &&
                        std::memcmp(data, kAssetVersion, sizeof(kAssetVersion) - 1) == 0;
  std::fclose(f);
  if (!marker_ok) return false;
  return true;
}
inline bool base_assets_ready(const char *root = kAssetRoot) {
  if (!root || install_in_progress(root) || !base_assets_present(root)) return false;
  return catalog_marker_matches(std::string(root) + kBaseReadyName, kBaseReadyVersion) ||
         catalog_marker_matches(std::string(root) + kReadyName, kAssetVersion);
}
inline bool assets_present_for_model(int id, const char *root = kAssetRoot) {
  return model_present_fast(id, root) && base_assets_present(root);
}
inline bool assets_ready_for_model(int id, const char *root = kAssetRoot) {
  return model_ready_fast(id, root) && base_assets_ready(root);
}
inline bool assets_present(const char *root = kAssetRoot) {
  return assets_present_for_model(kDefaultModelId, root);
}
inline bool assets_ready(const char *root = kAssetRoot) {
  return assets_ready_for_model(kDefaultModelId, root);
}
} // namespace onion::hyper
