#include "plugin_manager_runtime.hpp"
#include "plugin_directory_watcher.hpp"

#include <elfldr_remote.h>
#include <onion/log.h>
#include <onion/plugin_manager.hpp>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>
#include <onion/fs.h>
#include "process-info.h"
#include "elf-image.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

namespace onion::daemon::plugins {
namespace {

struct DispatchReceipt {
  uint64_t magic = UINT64_C(0x4859504c55470001), fingerprint = 0;
  int64_t boot_seconds = 0, boot_microseconds = 0;
  int64_t seconds = 0, microseconds = 0;
  int32_t pid = -1;
  uint32_t state = 0; // 0=intent/unknown, 1=confirmed process identity
  char id[10]{};
};
bool boot_time(timeval &boot) {
  int mib[] = {CTL_KERN, KERN_BOOTTIME}; size_t size = sizeof(boot);
  return sysctl(mib, 2, &boot, &size, nullptr, 0) == 0 && size == sizeof(boot);
}
std::string dispatch_path(std::string_view id) {
  return std::string(ONION_SYSTEM_TMP_ROOT "/plugin-dispatch/") + std::string(id) + ".state";
}
int dispatch_read(std::string_view id, DispatchReceipt &r) {
  const std::string path = dispatch_path(id);
  int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if(fd < 0) return errno == ENOENT ? 0 : -1;
  struct stat st{}; timeval boot{};
  bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == sizeof(r) &&
            read(fd, &r, sizeof(r)) == sizeof(r) && boot_time(boot) &&
            r.magic == UINT64_C(0x4859504c55470001) &&
            strnlen(r.id, sizeof(r.id)) == id.size() && id == r.id && r.state <= 1;
  close(fd);
  if(!ok) return -1;
  if(r.boot_seconds != boot.tv_sec || r.boot_microseconds != boot.tv_usec) return 0;
  return 1;
}
bool dispatch_save(std::string_view id, const DispatchReceipt &r) {
  if(mkdir(ONION_SYSTEM_TMP_ROOT, 0777) && errno != EEXIST) return false;
  if(mkdir(ONION_SYSTEM_TMP_ROOT "/plugin-dispatch", 0700) && errno != EEXIST) return false;
  return write_file_atomic(dispatch_path(id).c_str(), &r, sizeof(r));
}

std::string pid_key(std::string_view plugin_id) {
  return "plugin_" + std::string(plugin_id);
}

std::string pid_path(std::string_view plugin_id) {
  char path[256]{};
  const std::string key = pid_key(plugin_id);
  if (!onion_system_tmp_pid_path(path, sizeof(path), key.c_str())) return {};
  return path;
}

class DaemonRuntime final : public plugin::ProcessRuntime {
  std::map<pid_t, std::pair<int64_t, int64_t>> plugin_processes_;

  bool owns_process(const DispatchReceipt &r) {
    kinfo_proc info{};
    if(r.state != 1 || r.pid <= 1 || hhp_process_info(r.pid, &info) ||
       r.seconds != info.ki_start.tv_sec || r.microseconds != info.ki_start.tv_usec ||
       strcmp(info.ki_comm, r.id)) return false;
    plugin_processes_[r.pid] = {r.seconds, r.microseconds};
    return true;
  }
public:
  pid_t recover_file(const plugin::PluginFile &file) override {
    if(file.format != plugin::PluginFormat::EtaHenContainer)
      return recover(file.descriptor.plugin_id);
    DispatchReceipt receipt;
    return dispatch_read(file.descriptor.plugin_id, receipt) == 1 &&
                   receipt.fingerprint == file.fingerprint && owns_process(receipt)
               ? receipt.pid : -1;
  }
  pid_t recover(std::string_view plugin_id) override {
    const std::string path = pid_path(plugin_id);
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) return -1;
    char value[32]{};
    const ssize_t count = read(descriptor, value, sizeof(value) - 1);
    close(descriptor);
    if (count <= 0) return -1;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    return errno == 0 && end != value && (*end == '\0' || *end == '\n') &&
                   parsed > 1
               ? static_cast<pid_t>(parsed)
               : -1;
  }

  pid_t launch(const plugin::PluginFile &plugin_file) override {
    if (!elfldr_remote_onion_available()) {
      LOG_ERROR("[plugins] private elfldr :%u unavailable for %s",
                ONION_ELFLDR_PORT, plugin_file.descriptor.plugin_id.c_str());
      return -1;
    }
    pid_t pid = -1;
    if (plugin_file.format == plugin::PluginFormat::EtaHenContainer) {
      HhpElfImage checked{};
      if(hhp_elf_inspect(plugin_file.image.data(), plugin_file.image.size(), &checked, nullptr, 0))
        return -1;
      DispatchReceipt receipt;
      const std::string &id = plugin_file.descriptor.plugin_id;
      const int previous = dispatch_read(id, receipt);
      if(previous != 0) {
        if(previous == 1 && receipt.fingerprint == plugin_file.fingerprint && owns_process(receipt))
          return receipt.pid;
        LOG_WARN("[plugins] %s has a prior/uncertain dispatch; console reboot required", id.c_str());
        return -1;
      }
      /* Keep argv[0] and the spawned process name equal to the plugin ID, as
       * expected by etaHEN-compatible standalone plugins.  The container
       * header itself is never sent to elfldr. */
      char staged_path[256]{};
      const int length = std::snprintf(staged_path, sizeof(staged_path),
                                       "%s/plugin-images/%s", ONION_SYSTEM_TMP_ROOT,
                                       plugin_file.descriptor.plugin_id.c_str());
      if (length <= 0 || static_cast<size_t>(length) >= sizeof(staged_path) ||
          plugin_file.image.empty())
        return -1;
      (void)mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
      if(mkdir(ONION_SYSTEM_TMP_ROOT "/plugin-images", 0700) && errno != EEXIST) return -1;
      if(!write_file_atomic(staged_path, plugin_file.image.data(), plugin_file.image.size())) return -1;
      timeval boot{};
      if(!boot_time(boot)) return -1;
      receipt = {}; receipt.fingerprint = plugin_file.fingerprint;
      receipt.boot_seconds = boot.tv_sec; receipt.boot_microseconds = boot.tv_usec;
      memcpy(receipt.id, id.c_str(), id.size()+1);
      // Persist BEFORE dispatch. A lost ACK must never cause a second spawn.
      if(!dispatch_save(id, receipt)) return -1;
      pid = elfldr_remote_onion_launch_plugin_file_get_pid(staged_path);
      (void)unlink(staged_path);
      kinfo_proc info{};
      if(pid <= 1 || hhp_process_info(pid, &info) || strcmp(info.ki_comm, id.c_str()) ||
         info.ki_start.tv_sec <= 0) {
        LOG_WARN("[plugins] %s dispatch outcome unknown; no automatic retry", id.c_str());
        return -1;
      }
      receipt.pid = pid; receipt.seconds = info.ki_start.tv_sec;
      receipt.microseconds = info.ki_start.tv_usec; receipt.state = 1;
      if(!dispatch_save(id, receipt) || !owns_process(receipt)) return -1;
    } else {
      pid = elfldr_remote_onion_launch_file_get_pid(plugin_file.path.c_str(),
                                                     nullptr);
    }
    if (pid > 1) {
      LOG_INFO("[plugins] dispatched %s %s (%s) format=%s pid=%d; plugin functionality not confirmed",
               plugin_file.descriptor.plugin_id.c_str(),
               plugin_file.descriptor.version.c_str(),
               plugin_file.descriptor.name.c_str(),
               plugin_file.format == plugin::PluginFormat::EtaHenContainer
                   ? "etahen-container"
                   : "hyperhen-elf",
               static_cast<int>(pid));
    }
    return pid;
  }

  bool alive(pid_t pid) override {
    const auto found = plugin_processes_.find(pid);
    if(found == plugin_processes_.end()) return onion_proc_is_alive(pid);
    kinfo_proc info{};
    return hhp_process_info(pid, &info) == 0 &&
           found->second.first == info.ki_start.tv_sec &&
           found->second.second == info.ki_start.tv_usec;
  }

  void persist(std::string_view plugin_id, pid_t pid) override {
    const std::string path = pid_path(plugin_id);
    if (path.empty()) return;
    if (pid <= 1) {
      unlink(path.c_str());
      return;
    }
    mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
    mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0) return;
    char value[32]{};
    const int length = std::snprintf(value, sizeof(value), "%d\n", pid);
    if (length > 0) (void)write(descriptor, value, static_cast<size_t>(length));
    close(descriptor);
  }

  void stop(pid_t pid, uint32_t flags) override {
    if (pid <= 1 || pid == getpid()) return;
    if (flags & plugin::kFlagStopSupported) {
      (void)kill(pid, SIGTERM);
      for (int attempt = 0; attempt < 10 && onion_proc_is_alive(pid); ++attempt)
        usleep(50 * 1000);
    }
    if (onion_proc_is_alive(pid)) (void)kill(pid, SIGKILL);
  }
};

plugin::Manager &manager() {
  static DaemonRuntime runtime;
  static plugin::Manager instance(plugin::Repository{}, runtime);
  return instance;
}

void watcher_reconcile(void *) { reconcile(); }

PluginDirectoryWatcher &watcher() {
  static PluginDirectoryWatcher instance(plugin::kInstallRoot,
                                         watcher_reconcile, nullptr);
  return instance;
}

void log_report(const char *operation, const plugin::ReconcileReport &report) {
  for (const plugin::DiscoveryIssue &issue : report.issues)
    LOG_WARN("[plugins] %s: %s", issue.path.c_str(), issue.message.c_str());
  LOG_INFO("[plugins] %s: discovered=%zu running=%zu started=%zu failed=%zu",
           operation, report.discovered, report.running, report.started,
           report.failed);
}

} // namespace

void start() {
  log_report("startup", manager().reconcile(true));
  if (!watcher().start())
    LOG_ERROR("[plugins] directory watcher failed to start");
}

void reconcile() {
  log_report("reconcile", manager().reconcile());
}

void stop() {
  watcher().stop();
  manager().stop_all();
  LOG_INFO("[plugins] stopped host-managed plugins; standalone residents are not forcibly unloaded");
}

std::vector<plugin::InventoryEntry> inventory() {
  // Recover discovery if the optional IPC listener was late at daemon startup.
  // Never replay boot autostart when a user opens the Plugins page.
  if(!watcher().running()) {
    log_report("inventory-recovery", manager().reconcile(false));
    (void)watcher().start();
  }
  return manager().inventory();
}

plugin::OperationResult start_plugin(std::string_view plugin_id) {
  return manager().start(plugin_id);
}

plugin::OperationResult stop_plugin(std::string_view plugin_id) {
  return manager().stop(plugin_id);
}

plugin::OperationResult reload_plugin(std::string_view plugin_id) {
  return manager().reload(plugin_id);
}

plugin::OperationResult remove_plugin(std::string_view plugin_id) {
  return manager().remove(plugin_id);
}

plugin::OperationResult set_auto_start(std::string_view plugin_id, bool enabled) {
  return manager().set_auto_start(plugin_id, enabled);
}

} // namespace onion::daemon::plugins
