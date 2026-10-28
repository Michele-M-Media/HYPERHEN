#include <onion/sprx_plugin_manager.hpp>

#include <onion/log.h>

#include <cstdlib>
#include <limits.h>
#include <mutex>
#include <set>

namespace onion::sprx {
namespace {

void append_issues(std::vector<SprxCatalogIssue> *dst,
                   const std::vector<SprxCatalogIssue> &src) {
  if (dst)
    dst->insert(dst->end(), src.begin(), src.end());
}

std::string_view basename_of(std::string_view path) noexcept {
  const size_t slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string canonical_module_name(std::string_view path) {
  const std::string input(path);
  char resolved[PATH_MAX] = {};
  if (realpath(input.c_str(), resolved))
    return std::string(basename_of(resolved));
  return std::string(basename_of(path));
}

} // namespace

SprxPluginManager::SprxPluginManager(IRemoteSprxRuntime &runtime,
                                     ITargetAccessPolicy &access_policy,
                                     ISprxTargetProvider &target_provider) noexcept
    : runtime_(runtime), access_policy_(access_policy),
      target_provider_(target_provider) {}

bool SprxPluginManager::load_catalog(
    std::string_view path, std::vector<SprxCatalogIssue> *issues) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (issues)
    issues->clear();
  return catalog_store_.load(path, issues);
}

SprxStartupReport SprxPluginManager::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  return start_locked();
}

SprxStartupReport SprxPluginManager::reconcile() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!catalog_store_.path().empty()) {
    std::vector<SprxCatalogIssue> issues;
    if (!catalog_store_.reload(&issues)) {
      return {false, std::move(issues), {}};
    }
  }
  return start_locked();
}

SprxStartupReport SprxPluginManager::start_locked(std::string_view manual_id) {
  SprxStartupReport report;
  report.catalog_loaded = catalog_store_.loaded();
  if (!report.catalog_loaded)
    return report;
  // TacHen workflow: saved autostart flags and game-start events never inject.
  if (manual_id.empty()) return report;

  SprxTarget target;
  if (!target_provider_.current(&target)) {
    LOG_DEBUG("[sprx-manager] no current game target");
    return report;
  }

  const SprxCatalog catalog = catalog_store_.snapshot();
  std::vector<SprxCatalogIssue> order_issues;
  const auto order = catalog.manual_order(manual_id, target.title_id, &order_issues);
  append_issues(&report.issues, order_issues);
  if (order.empty())
    return report;

  TitleIdAllowlist allowlist;
  if (!allowlist.add_exact(target.title_id)) {
    report.issues.push_back({0, {}, "invalid current target Title ID"});
    return report;
  }
  SprxLoader loader(runtime_, access_policy_, allowlist);
  std::set<std::string> failed;
  for (const SprxManifestEntry *entry : order) {
    SprxPluginResult item;
    item.id = entry->id;
    for (const std::string &dependency : entry->dependencies) {
      if (failed.contains(dependency)) {
        item.skipped_dependency = true;
        failed.insert(entry->id);
        break;
      }
    }
    if (!item.skipped_dependency) {
      if (target.version.empty() ||
          (!entry->game_version.empty() && entry->game_version != target.version)) {
        item.load.status = LoadStatus::PolicyDenied;
        failed.insert(entry->id); report.results.push_back(std::move(item)); continue;
      }
      if (entry->game_version.empty() && !catalog_store_.bind_game(entry->id,target.title_id,target.version)) {
        item.load.status = LoadStatus::WriteFailed;
        failed.insert(entry->id); report.results.push_back(std::move(item)); continue;
      }
      LoadRequest request;
      request.pid = target.pid;
      request.title_id = target.title_id;
      request.path = entry->path;
      request.options.max_attempts = 1;
      request.options.game_version = target.version;
      item.load = loader.load(request);
      if (!item.load.succeeded())
        failed.insert(entry->id);
    }
    report.results.push_back(std::move(item));
  }
  return report;
}

std::vector<SprxInventoryEntry> SprxPluginManager::inventory() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SprxInventoryEntry> entries = catalog_store_.inventory();
  for (auto &entry : entries) entry.auto_start = false;
  const SprxCatalog catalog = catalog_store_.snapshot();
  SprxTarget target;
  if (!target_provider_.current(&target))
    return entries;
  for (SprxInventoryEntry &item : entries) {
    const SprxManifestEntry *entry = catalog.find(item.id);
    if (!entry || !catalog.matches(*entry, target.title_id) ||
        (!entry->game_version.empty() && entry->game_version != target.version))
      continue;
    item.matches_current_target = true;
    ModuleInfo module;
    const std::string module_name = canonical_module_name(item.path);
    item.loaded_for_current_target = !module_name.empty() &&
                                     runtime_.find_loaded(target.pid, module_name,
                                                          &module);
  }
  return entries;
}

SprxOperationResult SprxPluginManager::set_enabled(std::string_view id,
                                                    bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  const SprxCatalog catalog = catalog_store_.snapshot();
  const auto *entry = catalog.find(id);
  if (!entry) return {false, "SPRX plugin is not declared"};
  SprxTarget target;
  if (!enabled) {
    ModuleInfo module;
    if (target_provider_.current(&target) &&
        runtime_.find_loaded(target.pid, canonical_module_name(entry->path), &module))
      return {false, "resident module cannot be generically unloaded; close the game"};
    return catalog_store_.set_enabled(id, false);
  }
  if (!target_provider_.current(&target) || !catalog.matches(*entry, target.title_id))
    return {false, "start the registered target game before loading this plugin"};
  const auto report = start_locked(id);
  if (!report.issues.empty()) return {false, report.issues.front().message};
  for (const auto &item : report.results)
    if (item.id == id)
      return item.load.succeeded() && !item.skipped_dependency
                 ? SprxOperationResult{true, {}}
                 : SprxOperationResult{false, load_status_name(item.load.status)};
  return {false, "plugin or its dependencies do not match the running game"};
}

SprxOperationResult SprxPluginManager::remove(std::string_view id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const SprxCatalog catalog = catalog_store_.snapshot();
  const SprxManifestEntry *entry = catalog.find(id);
  if (!entry)
    return {false, "SPRX plugin is not declared"};
  SprxTarget target;
  if (target_provider_.current(&target) &&
      catalog.matches(*entry, target.title_id)) {
    ModuleInfo module;
    const std::string module_name = canonical_module_name(entry->path);
    if (!module_name.empty() && runtime_.find_loaded(target.pid, module_name,
                                                     &module)) {
      return {false, "SPRX plugin is loaded in the current game"};
    }
  }
  return catalog_store_.remove(id);
}

void SprxPluginManager::stop() noexcept {
  /* SPRX modules belong to the target process; unload is intentionally absent. */
}

} // namespace onion::sprx
