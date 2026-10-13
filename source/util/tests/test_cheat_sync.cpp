#include "test_harness.h"

#include "cheats/sync/cheat_sync_engine.hpp"
#include "cheats/sync/cheat_sync_service.hpp"
#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/i_cheat_mirror.hpp"
#include "cheats/sync/i_http_transport.hpp"

#include <onion/notify.h>

#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using onion::cheats::sync::CheatMirrorId;
using onion::cheats::sync::CheatSyncEngine;
using onion::cheats::sync::CheatSyncService;
using onion::cheats::sync::CheatSyncStatus;
using onion::cheats::sync::HttpRequest;
using onion::cheats::sync::ICheatCatalog;
using onion::cheats::sync::ICheatMirror;
using onion::cheats::sync::IHttpTransport;
using onion::cheats::sync::SyncCancelFn;
using onion::cheats::sync::SyncStatus;

namespace {

using namespace std::chrono_literals;

extern "C" int32_t sceKernelSendNotificationRequest(int32_t device, void *req,
                                                     size_t size,
                                                     int32_t blocking);

std::vector<std::string> g_flatten_roots;
int g_flatten_result = 0;

struct ProgressEvent {
  std::string phase;
  size_t completed = 0;
  size_t total = 0;
};

std::vector<ProgressEvent> g_progress_events;

struct CancelState {
  int checks = 0;
  int cancel_after = 0;
};

struct PhaseCancelState {
  const char *phase = nullptr;
  size_t after_completed = 0;
  bool requested = false;
};

bool cancel_after_checks(void *user) {
  auto *state = static_cast<CancelState *>(user);
  return state && ++state->checks >= state->cancel_after;
}

bool phase_cancel_requested(void *user) {
  const auto *state = static_cast<const PhaseCancelState *>(user);
  return state && state->requested;
}

const std::filesystem::path &test_root() {
  static const std::filesystem::path root =
      std::filesystem::path(ONION_DATA_ROOT) / "sync-engine";
  return root;
}

class FakeCatalog final : public ICheatCatalog {
public:
  const char *id() const override { return "fake-collection"; }
  const char *defaultBranch() const override { return "main"; }
  const char *slugFor(CheatMirrorId) const override { return "org/fake"; }
  const char *const *flattenRoots(size_t *count) const override {
    static const char *const kRoots[] = {"cheats"};
    if (count) {
      *count = 1;
    }
    return kRoots;
  }
};

class FakeMirror final : public ICheatMirror {
public:
  FakeMirror(CheatMirrorId id, const char *name, const char *host,
             const char *url)
      : id_(id), name_(name), host_(host), url_(url) {}
  CheatMirrorId id() const override { return id_; }
  const char *name() const override { return name_; }
  const char *archiveHost() const override { return host_; }
  std::string archiveUrl(const ICheatCatalog &) const override { return url_; }

private:
  CheatMirrorId id_;
  const char *name_;
  const char *host_;
  const char *url_;
};

class MockHttp final : public IHttpTransport {
public:
  std::vector<SyncStatus> responses{SyncStatus::Ok};
  std::vector<unsigned char> archive;
  std::vector<std::string> urls;
  std::vector<std::string> hosts;
  size_t max_body_bytes = 0;

  SyncStatus perform(
      const HttpRequest &req,
      const std::function<SyncStatus(const void *, size_t)> &on_data) override {
    urls.emplace_back(req.url ? req.url : "");
    hosts.emplace_back(req.host_allow ? req.host_allow : "");
    max_body_bytes = req.max_body_bytes;
    const size_t index = urls.size() - 1;
    const SyncStatus response =
        index < responses.size() ? responses[index] : responses.back();
    if (response != SyncStatus::Ok) {
      return response;
    }
    if (req.on_progress) {
      req.on_progress(archive.size(), archive.size(), req.progress_user);
    }
    return on_data && !archive.empty()
               ? on_data(archive.data(), archive.size())
               : SyncStatus::Ok;
  }
};

class BlockingCancelHttp final : public IHttpTransport {
public:
  SyncStatus perform(
      const HttpRequest &req,
      const std::function<SyncStatus(const void *, size_t)> &) override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      entered_ = true;
    }
    cv_.notify_all();

    if (req.on_progress) {
      req.on_progress(1, 10, req.progress_user);
    }

    std::unique_lock<std::mutex> lock(mu_);
    while (!released_) {
      lock.unlock();
      const bool cancelled =
          req.should_cancel && req.should_cancel(req.cancel_user);
      lock.lock();
      if (cancelled) {
        cancel_seen_ = true;
        cv_.notify_all();
      }
      cv_.wait_for(lock, 1ms, [this] { return released_; });
    }
    lock.unlock();
    return req.should_cancel && req.should_cancel(req.cancel_user)
               ? SyncStatus::Cancelled
               : SyncStatus::Network;
  }

  bool waitUntilEntered() {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, 2s, [this] { return entered_; });
  }

  bool waitUntilCancelSeen() {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, 2s, [this] { return cancel_seen_; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      released_ = true;
    }
    cv_.notify_all();
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mu_);
    entered_ = false;
    cancel_seen_ = false;
    released_ = false;
  }

private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool entered_ = false;
  bool cancel_seen_ = false;
  bool released_ = false;
};

struct CapturedNotification {
  std::string message;
  bool debug = false;
};

std::mutex g_notification_mu;
std::vector<CapturedNotification> g_notifications;

int32_t capture_notification(int32_t, void *request, size_t size, int32_t) {
  constexpr size_t kIconFlagOffset = 0x2c;
  constexpr size_t kMessageOffset = 0x2d;
  if (!request || size <= kMessageOffset) {
    return 0;
  }
  const auto *bytes = static_cast<const unsigned char *>(request);
  const char *message = reinterpret_cast<const char *>(bytes + kMessageOffset);
  std::lock_guard<std::mutex> lock(g_notification_mu);
  g_notifications.push_back(
      CapturedNotification{message, bytes[kIconFlagOffset] == 0});
  return 0;
}

size_t notification_count(const char *message, bool debug) {
  std::lock_guard<std::mutex> lock(g_notification_mu);
  return static_cast<size_t>(std::count_if(
      g_notifications.begin(), g_notifications.end(),
      [message, debug](const CapturedNotification &notification) {
        return notification.debug == debug && notification.message == message;
      }));
}

bool wait_until_notification(const char *message, bool debug,
                             size_t expected_count) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (notification_count(message, debug) >= expected_count) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

bool wait_until_not_running(CheatSyncService &service) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (service.status().state != CheatSyncStatus::State::Running) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

class ServiceTestScope {
public:
  ServiceTestScope(CheatSyncService &service, BlockingCancelHttp &http)
      : service_(service), http_(http) {
    {
      std::lock_guard<std::mutex> lock(g_notification_mu);
      g_notifications.clear();
    }
    onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
    onion_notify_set_send(capture_notification);
    service_.setHttpTransportForTest(&http_);
  }

  ~ServiceTestScope() {
    const CheatSyncStatus current = service_.status();
    if (current.state == CheatSyncStatus::State::Running) {
      (void)service_.cancel(current.task_id);
    }
    http_.release();
    (void)wait_until_not_running(service_);
    service_.setHttpTransportForTest(nullptr);
    onion_notify_set_send(sceKernelSendNotificationRequest);
    onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  }

private:
  CheatSyncService &service_;
  BlockingCancelHttp &http_;
};

std::vector<unsigned char> make_archive() {
  mz_zip_archive zip{};
  void *data = nullptr;
  size_t size = 0;
  const char cheat[] = "{\"name\":\"fixture\"}";
  const char ignored[] = "ignored";
  if (!mz_zip_writer_init_heap(&zip, 0, 0) ||
      !mz_zip_writer_add_mem(&zip, "repo-main/cheats/game.json", cheat,
                             sizeof(cheat) - 1, MZ_BEST_SPEED) ||
      !mz_zip_writer_add_mem(&zip, "repo-main/cheats/game2.shn", cheat,
                             sizeof(cheat) - 1, MZ_BEST_SPEED) ||
      !mz_zip_writer_add_mem(&zip, "repo-main/website/index.html", ignored,
                             sizeof(ignored) - 1, MZ_BEST_SPEED) ||
      !mz_zip_writer_finalize_heap_archive(&zip, &data, &size)) {
    mz_zip_writer_end(&zip);
    return {};
  }
  std::vector<unsigned char> out(static_cast<unsigned char *>(data),
                                 static_cast<unsigned char *>(data) + size);
  mz_free(data);
  mz_zip_writer_end(&zip);
  return out;
}

SyncStatus capture_flatten(const char *root,
                           CheatSyncEngine::InstallProgressFn progress,
                           void *progress_user, SyncCancelFn should_cancel,
                           void *cancel_user) {
  g_flatten_roots.emplace_back(root ? root : "");
  if (g_flatten_result != 0 || !root) {
    return SyncStatus::Io;
  }
  std::ifstream input(std::filesystem::path(root) / "game.json");
  std::string contents;
  std::getline(input, contents);
  if (contents != "{\"name\":\"fixture\"}") {
    return SyncStatus::Io;
  }
  if (progress) {
    progress(0, 3, progress_user);
    for (size_t completed = 1; completed <= 3; ++completed) {
      progress(completed, 3, progress_user);
      if (should_cancel && should_cancel(cancel_user)) {
        return SyncStatus::Cancelled;
      }
    }
  }
  return SyncStatus::Ok;
}

void capture_progress(const char *phase, size_t completed, size_t total,
                      void *) {
  g_progress_events.push_back(
      ProgressEvent{phase ? phase : "", completed, total});
}

void capture_progress_and_cancel(const char *phase, size_t completed,
                                 size_t total, void *user) {
  capture_progress(phase, completed, total, nullptr);
  auto *state = static_cast<PhaseCancelState *>(user);
  if (state && phase && state->phase && phase == std::string(state->phase) &&
      completed >= state->after_completed) {
    state->requested = true;
  }
}

void reset_test_root() {
  std::error_code error;
  std::filesystem::remove_all(test_root(), error);
  std::filesystem::create_directories(test_root(), error);
  g_flatten_roots.clear();
  g_flatten_result = 0;
  g_progress_events.clear();
}

bool temp_was_removed() {
  return !std::filesystem::exists(test_root() / "cheats_tmp");
}

} // namespace

static int test_download_extract_install_cleanup(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "codeload.github.com",
                    "https://codeload.github.com/org/fake/zip/refs/heads/main");
  MockHttp http;
  http.archive = make_archive();
  TEST_ASSERT_TRUE(!http.archive.empty());
  CheatSyncEngine engine(http, capture_flatten);
  engine.setProgressHandler(capture_progress, nullptr);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Ok);
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_STREQ("codeload.github.com", http.hosts[0].c_str());
  TEST_ASSERT_TRUE(http.max_body_bytes == 64ull * 1024ull * 1024ull);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(g_flatten_roots.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  const auto install_begin = std::find_if(
      g_progress_events.begin(), g_progress_events.end(),
      [](const ProgressEvent &event) {
        return event.phase == "install" && event.completed == 0;
      });
  const auto install_end = std::find_if(
      g_progress_events.begin(), g_progress_events.end(),
      [](const ProgressEvent &event) {
        return event.phase == "install" && event.completed == 3 &&
               event.total == 3;
      });
  const auto cleanup = std::find_if(
      g_progress_events.begin(), g_progress_events.end(),
      [](const ProgressEvent &event) { return event.phase == "cleanup"; });
  TEST_ASSERT_TRUE(install_begin != g_progress_events.end());
  TEST_ASSERT_TRUE(install_end != g_progress_events.end());
  TEST_ASSERT_TRUE(cleanup != g_progress_events.end());
  TEST_ASSERT_TRUE(install_begin < install_end);
  TEST_ASSERT_TRUE(install_end < cleanup);
  return 0;
}

static int test_network_failure_uses_fallback(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror primary(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                     "https://cnb.cool/org/fake/archive.zip");
  FakeMirror fallback(CheatMirrorId::Github, "github", "codeload.github.com",
                      "https://codeload.github.com/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Network, SyncStatus::Ok};
  http.archive = make_archive();
  CheatSyncEngine engine(http, capture_flatten);
  const auto result =
      engine.run(catalog, primary, &fallback, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Ok);
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(http.urls.size()));
  TEST_ASSERT_STREQ("cnb.cool", http.hosts[0].c_str());
  TEST_ASSERT_STREQ("codeload.github.com", http.hosts[1].c_str());
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_explicit_source_failure_does_not_fallback(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                    "https://cnb.cool/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Network};
  CheatSyncEngine engine(http, capture_flatten);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Network);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(g_flatten_roots.empty());
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_install_failure_does_not_switch_source(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror primary(CheatMirrorId::Github, "github", "codeload.github.com",
                     "https://codeload.github.com/org/fake/archive.zip");
  FakeMirror fallback(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                      "https://cnb.cool/org/fake/archive.zip");
  MockHttp http;
  http.archive = make_archive();
  g_flatten_result = -1;
  CheatSyncEngine engine(http, capture_flatten);
  const auto result =
      engine.run(catalog, primary, &fallback, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Io);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_non_https_archive_is_rejected(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "example.test",
                    "http://example.test/archive.zip");
  MockHttp http;
  CheatSyncEngine engine(http, capture_flatten);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Rejected);
  TEST_ASSERT_TRUE(http.urls.empty());
  return 0;
}

static int test_tls_failure_uses_fallback(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror primary(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                     "https://cnb.cool/org/fake/archive.zip");
  FakeMirror fallback(CheatMirrorId::Github, "github", "codeload.github.com",
                      "https://codeload.github.com/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Tls, SyncStatus::Ok};
  http.archive = make_archive();
  CheatSyncEngine engine(http, capture_flatten);
  const auto result =
      engine.run(catalog, primary, &fallback, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Ok);
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_clock_failure_sets_specific_error(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                    "https://cnb.cool/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Clock};
  CheatSyncEngine engine(http, capture_flatten);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Clock);
  TEST_ASSERT_STREQ("system_clock", result.error.c_str());
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_cancel_during_download_cleans_temp(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "codeload.github.com",
                    "https://codeload.github.com/org/fake/archive.zip");
  MockHttp http;
  http.archive = make_archive();
  CancelState cancel{0, 2};
  CheatSyncEngine engine(http, capture_flatten);
  engine.setCancelHandler(cancel_after_checks, &cancel);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Cancelled);
  TEST_ASSERT_TRUE(g_flatten_roots.empty());
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_cancel_during_extract_cleans_temp(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "codeload.github.com",
                    "https://codeload.github.com/org/fake/zip/refs/heads/main");
  MockHttp http;
  http.archive = make_archive();
  PhaseCancelState cancel{"extract", 1, false};
  CheatSyncEngine engine(http, capture_flatten);
  engine.setProgressHandler(capture_progress_and_cancel, &cancel);
  engine.setCancelHandler(phase_cancel_requested, &cancel);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Cancelled);
  TEST_ASSERT_TRUE(g_flatten_roots.empty());
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_cancel_during_install_cleans_temp(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "codeload.github.com",
                    "https://codeload.github.com/org/fake/zip/refs/heads/main");
  MockHttp http;
  http.archive = make_archive();
  PhaseCancelState cancel{"install", 1, false};
  CheatSyncEngine engine(http, capture_flatten);
  engine.setProgressHandler(capture_progress_and_cancel, &cancel);
  engine.setCancelHandler(phase_cancel_requested, &cancel);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Cancelled);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(g_flatten_roots.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_service_cancel_lifecycle(void) {
  const std::filesystem::path service_temp =
      std::filesystem::path(ONION_DATA_ROOT) / "cheats_tmp";
  std::error_code error;
  std::filesystem::remove_all(service_temp, error);

  CheatSyncService &service = CheatSyncService::instance();
  BlockingCancelHttp http;
  ServiceTestScope scope(service, http);
  onion::Settings settings;
  settings.ui_lang = onion::kUiLanguageZhHans;

  const char *const cancelling_key = "notify.cheats.sync.cancelling";
  const std::string cancelling_text = onion_notify_tr(cancelling_key);
  TEST_ASSERT_TRUE(cancelling_text != cancelling_key);
  const char *const cancelled_key = "notify.cheats.sync.cancelled";
  const std::string cancelled_text = onion_notify_tr(cancelled_key);
  TEST_ASSERT_TRUE(cancelled_text != cancelled_key);

  uint32_t first_task = 0;
  TEST_ASSERT_TRUE(service.start(settings, "hen-cheats-collection", "github",
                                 &first_task) ==
                   CheatSyncService::StartResult::Started);
  TEST_ASSERT_TRUE(first_task != 0);
  TEST_ASSERT_TRUE(http.waitUntilEntered());

  uint32_t busy_task = 0;
  TEST_ASSERT_TRUE(service.start(settings, "hen-cheats-collection", "github",
                                 &busy_task) ==
                   CheatSyncService::StartResult::AlreadyRunning);
  TEST_ASSERT_EQ_U64(first_task, busy_task);
  TEST_ASSERT_TRUE(!service.cancel(0));
  TEST_ASSERT_TRUE(!service.cancel(first_task + 1));
  TEST_ASSERT_EQ_U64(0, notification_count(cancelling_text.c_str(), true));
  TEST_ASSERT_EQ_U64(0, notification_count(cancelled_text.c_str(), true));

  TEST_ASSERT_TRUE(service.cancel(first_task));
  const CheatSyncStatus cancelling = service.status();
  TEST_ASSERT_TRUE(cancelling.state == CheatSyncStatus::State::Running);
  TEST_ASSERT_STREQ("cancel", cancelling.phase.c_str());
  TEST_ASSERT_EQ_INT(-1, cancelling.progress_percent);
  TEST_ASSERT_TRUE(service.cancellationRequested());
  TEST_ASSERT_EQ_U64(1, notification_count(cancelling_text.c_str(), true));

  TEST_ASSERT_TRUE(!service.cancel(first_task));
  TEST_ASSERT_EQ_U64(1, notification_count(cancelling_text.c_str(), true));

  service.noteProgress("cleanup", 100, 99, 99);
  const CheatSyncStatus after_late_progress = service.status();
  TEST_ASSERT_STREQ("cancel", after_late_progress.phase.c_str());
  TEST_ASSERT_EQ_INT(-1, after_late_progress.progress_percent);
  TEST_ASSERT_EQ_U64(cancelling.completed, after_late_progress.completed);
  TEST_ASSERT_EQ_U64(cancelling.total, after_late_progress.total);
  TEST_ASSERT_TRUE(http.waitUntilCancelSeen());
  TEST_ASSERT_TRUE(std::filesystem::exists(service_temp));
  TEST_ASSERT_EQ_U64(0, notification_count(cancelled_text.c_str(), true));

  http.release();
  TEST_ASSERT_TRUE(wait_until_not_running(service));
  TEST_ASSERT_TRUE(wait_until_notification(cancelled_text.c_str(), true, 1));
  const CheatSyncStatus cancelled = service.status();
  TEST_ASSERT_TRUE(cancelled.state == CheatSyncStatus::State::Idle);
  TEST_ASSERT_EQ_U64(first_task, cancelled.task_id);
  TEST_ASSERT_TRUE(cancelled.error.empty());
  TEST_ASSERT_TRUE(!service.cancellationRequested());
  TEST_ASSERT_TRUE(!std::filesystem::exists(service_temp));
  TEST_ASSERT_EQ_U64(1, notification_count(cancelling_text.c_str(), true));
  TEST_ASSERT_EQ_U64(1, notification_count(cancelled_text.c_str(), true));

  http.reset();
  uint32_t second_task = 0;
  TEST_ASSERT_TRUE(service.start(settings, "hen-cheats-collection", "github",
                                 &second_task) ==
                   CheatSyncService::StartResult::Started);
  TEST_ASSERT_TRUE(second_task != 0 && second_task != first_task);
  TEST_ASSERT_TRUE(http.waitUntilEntered());
  TEST_ASSERT_TRUE(!service.cancel(first_task));
  TEST_ASSERT_TRUE(service.cancel(second_task));
  TEST_ASSERT_TRUE(http.waitUntilCancelSeen());
  http.release();
  TEST_ASSERT_TRUE(wait_until_not_running(service));
  TEST_ASSERT_TRUE(wait_until_notification(cancelled_text.c_str(), true, 2));
  TEST_ASSERT_TRUE(service.status().state == CheatSyncStatus::State::Idle);
  TEST_ASSERT_TRUE(!std::filesystem::exists(service_temp));
  TEST_ASSERT_EQ_U64(2, notification_count(cancelling_text.c_str(), true));
  TEST_ASSERT_EQ_U64(2, notification_count(cancelled_text.c_str(), true));
  return 0;
}

extern "C" int test_cheat_sync_suite(void) {
  int fails = 0;
  fails += onion_test_run("sync.archive_install",
                          test_download_extract_install_cleanup);
  fails += onion_test_run("sync.fallback", test_network_failure_uses_fallback);
  fails += onion_test_run("sync.no_fallback",
                          test_explicit_source_failure_does_not_fallback);
  fails += onion_test_run("sync.install_no_switch",
                          test_install_failure_does_not_switch_source);
  fails += onion_test_run("sync.reject_http", test_non_https_archive_is_rejected);
  fails += onion_test_run("sync.tls_fallback", test_tls_failure_uses_fallback);
  fails += onion_test_run("sync.clock_error", test_clock_failure_sets_specific_error);
  fails += onion_test_run("sync.cancel_download",
                          test_cancel_during_download_cleans_temp);
  fails += onion_test_run("sync.cancel_extract",
                          test_cancel_during_extract_cleans_temp);
  fails += onion_test_run("sync.cancel_install",
                          test_cancel_during_install_cleans_temp);
  fails += onion_test_run("sync.service_cancel_lifecycle",
                          test_service_cancel_lifecycle);
  return fails;
}
