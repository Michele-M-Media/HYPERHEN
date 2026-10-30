// Generated from official pinned Hugging Face API metadata; no model bytes embedded.
#pragma once
#include <onion/hyper_assets_manifest.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace onion::hyper {
struct HyperModelEntry {
  int id;
  const char *label;
  const char *repository;
  const char *revision;
  const char *quantization;
  const ModelFile *parts;
  size_t part_count;
  uint64_t size_bytes;
  bool experimental;
  bool cpu_latency_verified;
  const char *ready_name;
  const char *ready_version;
};
inline constexpr int kDefaultModelId = 0;
inline constexpr ModelFile kModelParts0[] = {
  {{"hyper-chat.gguf", 491400032ull, "74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db"}, "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/9217f5db79a29953eb74d5343926648285ec7e67/qwen2.5-0.5b-instruct-q4_k_m.gguf"},
};
inline constexpr ModelFile kModelParts1[] = {
  {{"qwen2.5-0.5b-instruct-q5_k_m.gguf", 522186592ull, "041474553fcabfc2a2d67903f9d2c2e50bd92528e670da4f33b5d0ce6e59fd55"}, "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/9217f5db79a29953eb74d5343926648285ec7e67/qwen2.5-0.5b-instruct-q5_k_m.gguf"},
};
inline constexpr ModelFile kModelParts2[] = {
  {{"qwen2.5-1.5b-instruct-q4_k_m.gguf", 1117320736ull, "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e"}, "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf"},
};
inline constexpr ModelFile kModelParts3[] = {
  {{"qwen2.5-1.5b-instruct-q5_k_m.gguf", 1285494304ull, "b46661073c18e5b56a41fa320975f866a00def1ff08feef4718e013258896f8c"}, "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q5_k_m.gguf"},
};
inline constexpr ModelFile kModelParts4[] = {
  {{"qwen2.5-3b-instruct-q4_k_m.gguf", 2104932768ull, "626b4a6678b86442240e33df819e00132d3ba7dddfe1cdc4fbb18e0a9615c62d"}, "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q4_k_m.gguf"},
};
inline constexpr ModelFile kModelParts5[] = {
  {{"qwen2.5-3b-instruct-q5_k_m.gguf", 2438740384ull, "2c63dde5f2c9ab1fd64d47dee2d34dade6ba9ff62442d1d20b5342310c982081"}, "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q5_k_m.gguf"},
};
inline constexpr ModelFile kModelParts6[] = {
  {{"qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf", 3993201344ull, "dfce12e3862a5283ccfb88221b48480e58745165de856439950d0f22590580db"}, "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/bb5d59e06d9551d752d08b292a50eb208b07ab1f/qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf"},
  {{"qwen2.5-7b-instruct-q4_k_m-00002-of-00002.gguf", 689872288ull, "539cf93f78e887edea1c04e2d7d8cdaca9d01dae9c9025bcb8accbe29df3d72a"}, "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/bb5d59e06d9551d752d08b292a50eb208b07ab1f/qwen2.5-7b-instruct-q4_k_m-00002-of-00002.gguf"},
};
inline constexpr ModelFile kModelParts7[] = {
  {{"qwen2.5-7b-instruct-q5_k_m-00001-of-00002.gguf", 3989841792ull, "42f6693004793ee6cf1b2b723f0273b10f86a3bb2a949bd9128d4cda5fb866cd"}, "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/bb5d59e06d9551d752d08b292a50eb208b07ab1f/qwen2.5-7b-instruct-q5_k_m-00001-of-00002.gguf"},
  {{"qwen2.5-7b-instruct-q5_k_m-00002-of-00002.gguf", 1454989568ull, "beba9d4f2f5a1fe7d144dcae332e68b52c26705c5310dece2e5d1997e091e134"}, "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/bb5d59e06d9551d752d08b292a50eb208b07ab1f/qwen2.5-7b-instruct-q5_k_m-00002-of-00002.gguf"},
};
inline constexpr HyperModelEntry kModelCatalog[] = {
  {0, "Qwen2.5 0.5B Q4_K_M", "Qwen/Qwen2.5-0.5B-Instruct-GGUF", "9217f5db79a29953eb74d5343926648285ec7e67", "Q4_K_M", kModelParts0, 1, 491400032ull, false, false, "/.model-ready-0", "hyper-model-r35-v1:45dea930f7310ae54de1b180aa43ef04a7762c8589bba095b2641ecd0a5eca5d\n"},
  {1, "Qwen2.5 0.5B Q5_K_M", "Qwen/Qwen2.5-0.5B-Instruct-GGUF", "9217f5db79a29953eb74d5343926648285ec7e67", "Q5_K_M", kModelParts1, 1, 522186592ull, true, false, "/.model-ready-1", "hyper-model-r35-v1:c9a50785fa115bcac79ab30d0e8f92ba941edac2251d620175cbe68d6b334d6e\n"},
  {2, "Qwen2.5 1.5B Q4_K_M", "Qwen/Qwen2.5-1.5B-Instruct-GGUF", "91cad51170dc346986eccefdc2dd33a9da36ead9", "Q4_K_M", kModelParts2, 1, 1117320736ull, true, false, "/.model-ready-2", "hyper-model-r35-v1:0380bebc2ec2f056bdcd58f05901c1e8891a57cbd3392c07354e35b0d1338512\n"},
  {3, "Qwen2.5 1.5B Q5_K_M", "Qwen/Qwen2.5-1.5B-Instruct-GGUF", "91cad51170dc346986eccefdc2dd33a9da36ead9", "Q5_K_M", kModelParts3, 1, 1285494304ull, true, false, "/.model-ready-3", "hyper-model-r35-v1:b3a8f0bdae6b5441dab0c5328cda4e523cec34ed1787dc52fc7e86a290523fb4\n"},
  {4, "Qwen2.5 3B Q4_K_M", "Qwen/Qwen2.5-3B-Instruct-GGUF", "7dabda4d13d513e3e842b20f0d435c732f172cbe", "Q4_K_M", kModelParts4, 1, 2104932768ull, true, false, "/.model-ready-4", "hyper-model-r35-v1:ab2752945a3f74a880de9d1c1e6e6ec85820fcf44428ed86f7aea2392b50cffc\n"},
  {5, "Qwen2.5 3B Q5_K_M", "Qwen/Qwen2.5-3B-Instruct-GGUF", "7dabda4d13d513e3e842b20f0d435c732f172cbe", "Q5_K_M", kModelParts5, 1, 2438740384ull, true, false, "/.model-ready-5", "hyper-model-r35-v1:c1bde6e732b9b087c0e1813a5145739037f84568a1d59df2059180192c21f200\n"},
  {6, "Qwen2.5 7B Q4_K_M", "Qwen/Qwen2.5-7B-Instruct-GGUF", "bb5d59e06d9551d752d08b292a50eb208b07ab1f", "Q4_K_M", kModelParts6, 2, 4683073632ull, true, false, "/.model-ready-6", "hyper-model-r35-v1:a40015a5efe2d109591832348303e4c347b115930ce1e652664a2bc8d0796fa1\n"},
  {7, "Qwen2.5 7B Q5_K_M", "Qwen/Qwen2.5-7B-Instruct-GGUF", "bb5d59e06d9551d752d08b292a50eb208b07ab1f", "Q5_K_M", kModelParts7, 2, 5444831360ull, true, false, "/.model-ready-7", "hyper-model-r35-v1:5c860c5b183ef6be84f57020a616a9df9c85eb62c384be2159d5c23c2c557c1c\n"},
};
inline constexpr size_t kModelCatalogCount = sizeof(kModelCatalog) / sizeof(kModelCatalog[0]);
inline constexpr size_t model_count() { return kModelCatalogCount; }
inline constexpr const HyperModelEntry *model_entry(int id) {
  return id >= 0 && static_cast<size_t>(id) < kModelCatalogCount ? &kModelCatalog[id] : nullptr;
}
inline constexpr const char *model_label(int id) {
  const auto *entry = model_entry(id); return entry ? entry->label : "";
}
inline constexpr uint64_t model_size_bytes(int id) {
  const auto *entry = model_entry(id); return entry ? entry->size_bytes : 0;
}
inline std::string model_primary_path(int id, const char *root = "/data/Hyper") {
  const auto *entry = model_entry(id);
  return entry && root ? std::string(root) + "/models/" + entry->parts[0].file.path : std::string();
}
inline bool catalog_regular_size(const std::string &path, uint64_t expected) {
  struct stat st {};
  return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
         st.st_size >= 0 && static_cast<uint64_t>(st.st_size) == expected;
}
inline bool catalog_marker_matches(const std::string &path, const char *version) {
  const size_t size = version ? std::strlen(version) : 0;
  if (!size || size >= 128 || !catalog_regular_size(path, size)) return false;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  char actual[128] {};
  const bool ok = std::fread(actual, 1, size, f) == size && std::memcmp(actual, version, size) == 0;
  std::fclose(f); return ok;
}
inline bool model_present_fast(int id, const char *root = "/data/Hyper") {
  const auto *entry = model_entry(id);
  if (!entry || !root) return false;
  for (size_t i = 0; i < entry->part_count; ++i)
    if (!catalog_regular_size(std::string(root) + "/models/" + entry->parts[i].file.path,
                              entry->parts[i].file.size)) return false;
  return true;
}
inline bool model_ready_fast(int id, const char *root = "/data/Hyper") {
  const auto *entry = model_entry(id);
  if (!entry || !root || !model_present_fast(id, root)) return false;
  const std::string base(root);
  return catalog_marker_matches(base + entry->ready_name, entry->ready_version) ||
         (id == 0 && catalog_marker_matches(base + "/.assets-ready", kAssetVersion));
}
} // namespace onion::hyper
