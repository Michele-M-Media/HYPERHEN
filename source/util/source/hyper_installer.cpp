#include "hyper_installer.hpp"
#include "hyper_installer_engine.hpp"
#include "cheats/sync/http_transport_ps5.hpp"
#include <onion/hyper_assets.hpp>
#include <onion/hyper_lifecycle.hpp>
#include <onion/log.h>
#include <onion/notify.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <array>
#include <vector>
#include <pthread.h>
#include <sys/file.h>
#include <unistd.h>

#if defined(HYPERHEN_INTEGRATED_HYPER)
extern "C" {
__asm__(
    ".pushsection .rodata\n"
    ".balign 1\n"
    ".global hyper_voice_zip_start\n"
    "hyper_voice_zip_start:\n"
    ".incbin \"" HYPER_VOICES_ZIP "\"\n"
    ".global hyper_voice_zip_end\n"
    "hyper_voice_zip_end:\n"
    ".popsection\n");
extern const unsigned char hyper_voice_zip_start[];
extern const unsigned char hyper_voice_zip_end[];
}
#endif

namespace onion::hyper {
namespace {
using onion::cheats::sync::SyncStatus;
struct Service {
  std::mutex mutex;
  std::atomic<bool> cancel{false};
  bool running = false;
  bool attempted = false;
  bool allow_download = true;
  int model_id = kDefaultModelId;
  const char *state = "missing";
  uint64_t received = 0;
  uint64_t total = 0;
  int progress = 0;
  int error = 0;
  int last_notified_percent = 0;
  std::chrono::steady_clock::time_point last_notified;
  bool cache_initialized = false;
  bool cache_scanning = false;
  unsigned cache_epoch = 0;
  bool base_ready = false;
  std::array<bool, kModelCatalogCount> model_ready{};
  std::array<bool, kModelCatalogCount> model_present{};
};
Service &service() { static Service state; return state; }
struct CatalogCache {
  bool base_ready = false;
  std::array<bool, kModelCatalogCount> ready{};
  std::array<bool, kModelCatalogCount> present{};
};
CatalogCache read_catalog_cache() {
  CatalogCache cache;
  cache.base_ready = base_assets_ready();
  for (const auto &entry : kModelCatalog) {
    cache.present[entry.id] = model_present_fast(entry.id);
    cache.ready[entry.id] = model_ready_fast(entry.id);
  }
  return cache;
}
void apply_catalog_cache(Service &svc, const CatalogCache &cache) {
  svc.base_ready = cache.base_ready;
  svc.model_ready = cache.ready;
  svc.model_present = cache.present;
  svc.cache_initialized = true;
  if (!svc.attempted) {
    const bool ready = cache.base_ready && cache.ready[0];
    svc.state = ready ? "ready" : "missing";
    svc.progress = ready ? 100 : 0;
  }
}
int start_detached(void *(*entry)(void *)) {
  pthread_attr_t attr;
  int rc = pthread_attr_init(&attr);
  if (rc != 0) return rc;
  rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  // Native pthread defaults are not a portable allowance for hashing/ZIP work.
  if (rc == 0) rc = pthread_attr_setstacksize(&attr, 1024u * 1024u);
  pthread_t thread;
  if (rc == 0) rc = pthread_create(&thread, &attr, entry, nullptr);
  (void)pthread_attr_destroy(&attr);
  return rc;
}
void *cache_worker(void *) {
  auto &svc = service();
  unsigned epoch;
  { std::lock_guard<std::mutex> lock(svc.mutex); epoch = svc.cache_epoch; }
  const CatalogCache cache = read_catalog_cache();
  std::lock_guard<std::mutex> lock(svc.mutex);
  if (epoch == svc.cache_epoch && !svc.running) apply_catalog_cache(svc, cache);
  svc.cache_scanning = false;
  return nullptr;
}
void ensure_cache_async() {
  auto &svc = service();
  std::lock_guard<std::mutex> lock(svc.mutex);
  if (svc.cache_initialized || svc.cache_scanning || svc.running) return;
  svc.cache_scanning = true;
  if (start_detached(cache_worker) != 0) svc.cache_scanning = false;
}
bool is_cancelled(void *) { return service().cancel.load(); }
struct WorkerGuard {
  int fd = -1;
  ~WorkerGuard() {
    if (fd >= 0) { (void)flock(fd, LOCK_UN); (void)close(fd); }
  }
};
[[maybe_unused]] SyncStatus wait_for_worker(void *user) {
  auto *guard = static_cast<WorkerGuard *>(user);
  guard->fd = open(kWorkerLock, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
  if (guard->fd < 0) return SyncStatus::Io;
  // Daemon observes .installing, cooperatively stops its owned worker, and
  // releases this existing singleton lock. Never signal arbitrary processes.
  for (unsigned i = 0; i < 150; ++i) {
    if (is_cancelled(nullptr)) return SyncStatus::Cancelled;
    if (flock(guard->fd, LOCK_EX | LOCK_NB) == 0) return SyncStatus::Ok;
    if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) return SyncStatus::Io;
    usleep(100000);
  }
  return SyncStatus::Busy;
}
[[maybe_unused]] void note_progress(const char *state, uint64_t received, uint64_t total, void *) {
  auto &svc = service();
  int notify_percent = -1;
  bool phase_changed = false;
  {
    std::lock_guard<std::mutex> lock(svc.mutex);
    phase_changed = std::strcmp(svc.state, state) != 0;
    svc.state = state;
    svc.received = received;
    svc.total = total;
    svc.progress = total ? static_cast<int>(std::min<uint64_t>(100, received * 100 / total)) : 0;
    const auto now = std::chrono::steady_clock::now();
    const int milestone = svc.progress / 5 * 5;
    if (milestone >= svc.last_notified_percent + 5 && milestone < 100 &&
        now - svc.last_notified >= std::chrono::seconds(15)) {
      svc.last_notified_percent = milestone;
      svc.last_notified = now;
      notify_percent = milestone;
    }
  }
  if (phase_changed)
    LOG_INFO("[Hyper] installer phase=%s received=%llu total=%llu", state,
             static_cast<unsigned long long>(received), static_cast<unsigned long long>(total));
  if (notify_percent >= 0)
    onion_notify_debug(0, "notify.hyper.install_progress", notify_percent);
}
void *worker(void *) {
  SyncStatus result = SyncStatus::Unavailable;
  bool allow_download = false;
  int model_id = -1;
#if defined(HYPERHEN_INTEGRATED_HYPER)
  { auto &svc = service(); std::lock_guard<std::mutex> lock(svc.mutex);
    allow_download = svc.allow_download; model_id = svc.model_id; }
  LOG_INFO("[Hyper] installer starting kind=%s model=%d",
           allow_download ? "download" : "offline-verify", model_id);
  onion::cheats::sync::Ps5HttpTransport http;
  const size_t zip_size = static_cast<size_t>(
      reinterpret_cast<uintptr_t>(hyper_voice_zip_end) -
      reinterpret_cast<uintptr_t>(hyper_voice_zip_start));
  if (zip_size == kVoiceZipBytes) {
    WorkerGuard guard;
    const auto *entry = model_entry(model_id);
    std::vector<ModelFile> files(entry->parts, entry->parts + entry->part_count);
    files.push_back(kCommonWhisperModel);
    InstallInputs inputs{kAssetRoot, files.data(), files.size(), kVoiceFiles,
        kVoiceFileCount, hyper_voice_zip_start, zip_size, kVoiceZipSha256,
        &http, note_progress, is_cancelled, &guard, wait_for_worker, allow_download};
    inputs.ready_name = entry->ready_name; inputs.ready_version = entry->ready_version;
    inputs.base_ready_name = kBaseReadyName; inputs.base_ready_version = kBaseReadyVersion;
    inputs.compat_ready_name = model_id == 0 ? kReadyName : nullptr;
    inputs.common_model_count = 1;
    result = install_assets(inputs);
  } else result = SyncStatus::Protocol;
#endif
  const CatalogCache cache = read_catalog_cache();
  {
    auto &svc = service();
    std::lock_guard<std::mutex> lock(svc.mutex);
    apply_catalog_cache(svc, cache);
    svc.running = false;
    svc.error = static_cast<int>(result);
    svc.state = result == SyncStatus::Ok ? "ready" :
                result == SyncStatus::Cancelled ? "cancelled" : "error";
    if (result == SyncStatus::Ok) { svc.progress = 100; svc.received = svc.total; }
  }
  LOG_INFO("[Hyper] installer finished kind=%s model=%d result=%d",
           allow_download ? "download" : "offline-verify", model_id, static_cast<int>(result));
  if (result == SyncStatus::Ok) onion_notify_debug(0, "notify.hyper.install_ready");
  else if (result == SyncStatus::Cancelled) onion_notify_debug(0, "notify.hyper.install_cancelled");
  else onion_notify_debug(0, "notify.hyper.install_failed", static_cast<int>(result));
  return nullptr;
}
} // namespace

static bool start_job(bool allow_download, int model_id) {
  if (!model_entry(model_id)) return false;
  auto &svc = service();
  std::unique_lock<std::mutex> lock(svc.mutex);
  if (svc.running) {
    lock.unlock();
    onion_notify_debug(0, "notify.hyper.install_busy");
    return false;
  }
  svc.attempted = true; svc.running = true; svc.cancel.store(false);
  svc.allow_download = allow_download;
  svc.model_id = model_id; ++svc.cache_epoch;
  svc.state = "verifying"; svc.error = 0; svc.progress = 0;
  svc.received = 0; svc.total = 0; svc.last_notified_percent = 0;
  svc.last_notified = std::chrono::steady_clock::now();
  const int rc = start_detached(worker);
  if (rc != 0) {
    svc.running = false; svc.state = "error";
    svc.error = static_cast<int>(SyncStatus::Unavailable);
    lock.unlock();
    onion_notify_debug(0, "notify.hyper.install_failed", static_cast<int>(SyncStatus::Unavailable));
    return false;
  }
  lock.unlock();
  if (allow_download) onion_notify_debug(0, "notify.hyper.install_started");
  return true;
}
bool start_install(int model_id) { return start_job(true, model_id); }
bool start_verify_existing(int model_id) { return start_job(false, model_id); }
bool cancel_install() {
  auto &svc = service();
  std::lock_guard<std::mutex> lock(svc.mutex);
  if (!svc.running) return false;
  svc.cancel.store(true);
  return true;
}
std::string install_status_json() {
  ensure_cache_async();
  auto &svc = service();
  std::lock_guard<std::mutex> lock(svc.mutex);
  char json[256];
  std::snprintf(json, sizeof(json),
      "{\"state\":\"%s\",\"progress\":%d,\"received\":%llu,\"total\":%llu,\"error_code\":%d,\"model_id\":%d}",
      svc.state, svc.progress, static_cast<unsigned long long>(svc.received),
      static_cast<unsigned long long>(svc.total), svc.error, svc.model_id);
  return json;
}
std::string catalog_status_json() {
  ensure_cache_async();
  auto &svc = service();
  std::lock_guard<std::mutex> lock(svc.mutex);
  std::string json = std::string("{\"cache_ready\":") +
      (svc.cache_initialized ? "true" : "false") + ",\"base_ready\":" +
      (svc.base_ready ? "true" : "false") + ",\"models\":[";
  for (const auto &entry : kModelCatalog) {
    char item[384];
    std::snprintf(item, sizeof(item),
        "%s{\"id\":%d,\"label\":\"%s\",\"size_bytes\":%llu,\"part_count\":%zu,"
        "\"experimental\":%s,\"cpu_latency_verified\":false,\"present\":%s,\"ready\":%s}",
        entry.id ? "," : "", entry.id, entry.label,
        static_cast<unsigned long long>(entry.size_bytes), entry.part_count,
        entry.experimental ? "true" : "false", svc.model_present[entry.id] ? "true" : "false",
        svc.base_ready && svc.model_ready[entry.id] ? "true" : "false");
    json += item;
  }
  json += "]}";
  return json;
}
} // namespace onion::hyper
