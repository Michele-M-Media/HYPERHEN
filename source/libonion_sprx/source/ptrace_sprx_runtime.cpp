/* SPDX-License-Identifier: GPL-3.0-or-later
 * HyperHEN game-owned plugin worker. Workflow follows TacHen's native
 * module loader, adapted to the existing Toolbox/ptrace transport.
 */
#include <onion/sprx_loader.hpp>
#include <onion/plugin_staging.hpp>
#include <onion/plugin_worker.h>
#include <onion/fs.h>
#include <onion/log.h>
#include <onion/pt.h>
#include <onion/system_tmp.h>
#include <onion/ucred.h>
#include "prx-image.h"
extern "C" {
#include "sha256.h"
extern const unsigned char hyperhen_plugin_worker_start[];
extern const unsigned char hyperhen_plugin_worker_end[];
}
#include <ps5/kernel.h>
#include <onion/proc_dmap.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

namespace onion::sprx {
namespace {
#ifndef HYPERHEN_PLUGIN_ATTEMPT_ROOT
#define HYPERHEN_PLUGIN_ATTEMPT_ROOT ONION_SYSTEM_TMP_ROOT "/plugin-modules"
#endif
constexpr size_t kPage = 0x4000;
constexpr uint64_t kReceiptMagic = UINT64_C(0x4859505258320001);
enum { Intent=1, Started=2, Failed=3 };
struct Receipt {
  uint64_t magic = kReceiptMagic;
  int64_t boot_seconds = 0, boot_microseconds = 0;
  int64_t seconds = 0, microseconds = 0;
  int32_t pid = -1;
  uint32_t state = Intent;
  int32_t handle = -1, initializer = -1, activation = -1;
  uint32_t reserved = 0;
  char name[256]{};
  uint64_t context = 0, load = 0, resolve = 0;
  char content_key[72]{};
  char staged_path[512]{};
  char metadata_hash[65]{};
};
bool boot_time(timeval &out) {
  int mib[] = {CTL_KERN, KERN_BOOTTIME}; size_t size = sizeof(out);
  return sysctl(mib, 2, &out, &size, nullptr, 0) == 0 && size == sizeof(out);
}
std::string receipt_path(const GameIdentity &id, std::string_view name) {
  SHA256_CTX sha; unsigned char digest[32]; char hex[65]{};
  sha256_init(&sha); sha256_update(&sha, reinterpret_cast<const uint8_t *>(name.data()), name.size()); sha256_final(&sha, digest);
  for(unsigned i=0;i<32;++i) snprintf(hex+i*2, 3, "%02x", digest[i]);
  char result[256];
  snprintf(result, sizeof(result), "%s/%d-%lld-%lld-%s.state",
           HYPERHEN_PLUGIN_ATTEMPT_ROOT, id.pid, static_cast<long long>(id.seconds),
           static_cast<long long>(id.microseconds), hex);
  return result;
}
int read_receipt(const GameIdentity &id, std::string_view name, Receipt &r) {
  const std::string path = receipt_path(id, name);
  int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if(fd < 0) return errno == ENOENT ? 0 : -1;
  struct stat st{}; timeval boot{};
  const bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
    st.st_size == sizeof(r) && read(fd, &r, sizeof(r)) == sizeof(r) && boot_time(boot) &&
    r.magic == kReceiptMagic && r.pid == id.pid && r.seconds == id.seconds &&
    r.microseconds == id.microseconds && r.boot_seconds == boot.tv_sec &&
    r.boot_microseconds == boot.tv_usec && strnlen(r.name, sizeof(r.name)) == name.size() &&
    name == r.name && strnlen(r.metadata_hash,sizeof(r.metadata_hash)) == 64 &&
    (r.state == Intent || r.state == Started || r.state == Failed);
  close(fd); return ok ? 1 : -1;
}
bool save_receipt(const GameIdentity &id, std::string_view name, const Receipt &r) {
  if(name.empty() || name.size() >= sizeof(r.name)) return false;
  if(mkdir(ONION_SYSTEM_TMP_ROOT, 0777) && errno != EEXIST) return false;
  if(mkdir(HYPERHEN_PLUGIN_ATTEMPT_ROOT, 0700) && errno != EEXIST) return false;
  Receipt stored = r;
  memset(stored.name, 0, sizeof(stored.name)); memcpy(stored.name, name.data(), name.size());
  return write_file_atomic(receipt_path(id, name).c_str(), &stored, sizeof(stored));
}
bool refresh_completion(const GameIdentity &id, std::string_view name, Receipt &r) {
  if(r.state != Intent || r.context < 0x10000 || r.context > UINT64_C(0x7fffffffffff)-sizeof(HyperhenPluginWorker) ||
     strnlen(r.content_key,sizeof(r.content_key)) != 71 ||
     strncmp(r.content_key,"sha256:",7) ||
     strnlen(r.staged_path,sizeof(r.staged_path)) == sizeof(r.staged_path) || !same_game(id)) return false;
  HyperhenPluginWorker ctx{};
  std::string version,metadata;
  if(!game_version(id,version,metadata) || metadata != r.metadata_hash) return false;
  if(onion_proc_copyout(id.pid,r.context,&ctx,sizeof(ctx)) || ctx.load_start_module!=r.load || ctx.dlsym!=r.resolve ||
     strnlen(ctx.path,sizeof(ctx.path)) == sizeof(ctx.path) || strcmp(ctx.path,r.staged_path) ||
     (ctx.state!=HH_PLUGIN_STARTED && ctx.state!=HH_PLUGIN_FAILED) || !same_game(id)) return false;
  r.handle=ctx.handle;r.initializer=ctx.start_result;r.activation=ctx.activation_result;
  r.state=ctx.state==HH_PLUGIN_STARTED?Started:Failed;
  if(!save_receipt(id,r.content_key,r) || !save_receipt(id,name,r)) {r.state=Intent;return false;}
  return true;
}
bool attempt_slot_available(const GameIdentity &id) {
  DIR *dir = opendir(HYPERHEN_PLUGIN_ATTEMPT_ROOT);
  if(!dir) return errno == ENOENT;
  char prefix[96]; snprintf(prefix, sizeof(prefix), "%d-%lld-%lld-", id.pid,
    static_cast<long long>(id.seconds), static_cast<long long>(id.microseconds));
  unsigned total = 0, inspected = 0;
  while(dirent *entry = readdir(dir)) {
    if(++inspected > 16384) { closedir(dir); return false; }
    if(strncmp(entry->d_name, prefix, strlen(prefix))) continue;
    int fd = openat(dirfd(dir), entry->d_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    Receipt r; struct stat st{};
    bool ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
       st.st_size == sizeof(r) && read(fd, &r, sizeof(r)) == sizeof(r) &&
       r.magic == kReceiptMagic && strnlen(r.name, sizeof(r.name)) < sizeof(r.name);
    if(fd >= 0) close(fd);
    if(!ok) { closedir(dir); return false; }
    if(!strncmp(r.name, "sha256:", 7)) ++total;
  }
  closedir(dir); return total < 8;
}
LoadResult result_for(LoadStatus status, pid_t pid, bool clean = true) {
  LoadResult r; r.status = r.underlying_status = status; r.pid = pid; r.cleanup_ok = clean; return r;
}
class Session {
  pid_t pid_; uintptr_t old_ = 0; bool attached_ = false;
public:
  explicit Session(pid_t pid) : pid_(pid) {
    old_ = set_ucred_to_ptrace();
    attached_ = old_ != 0 && pt_attach(pid_) == 0;
  }
  bool ready() const { return attached_; }
  bool detach() {
    if(!attached_) return true;
    if(pt_detach(pid_, 0)) return false;
    attached_ = false; return true;
  }
  ~Session() { detach(); if(old_) (void)set_proc_authid(getpid(), old_); }
};
intptr_t symbol(pid_t pid, const char *name) {
  intptr_t address = kernel_dynlib_dlsym(pid, 1, name);
  return address ? address : kernel_dynlib_dlsym(pid, 0x2001, name);
}
uint32_t host_sdk(pid_t pid) {
  intptr_t scratch = pt_mmap(pid, 0, kPage, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(scratch <= 0) return 0;
  uint64_t address = 0, size = 0; unsigned char param[32]{}; uint32_t sdk = 0;
  if(!pt_syscall(pid, 598, scratch, scratch+8, 0, 0, 0, 0) &&
     !pt_copyout(pid, scratch, &address, 8) && !pt_copyout(pid, scratch+8, &size, 8) &&
     address >= 0x10000 && address <= UINT64_C(0x7fffffffffff)-32 && size >= 32 && size <= 4096 &&
     !pt_copyout(pid, address, param, sizeof(param))) sdk = hhp_ps5_process_sdk(param, sizeof(param));
  (void)pt_munmap(pid, scratch, kPage); return sdk;
}
}

bool PtraceSprxRuntime::find_loaded(pid_t pid, std::string_view name, ModuleInfo *out) const noexcept {
  if(!out || name.empty() || name.size() >= 256) return false;
  GameIdentity id; Receipt r;
  if(!capture_game(pid, id) || read_receipt(id, name, r) != 1) return false;
  std::string version,metadata;
  if(!game_version(id,version,metadata) || metadata != r.metadata_hash) return false;
  (void)refresh_completion(id,name,r);
  if(r.state != Started || r.handle < 0 || r.initializer < 0 || r.activation < 0 || !same_game(id)) return false;
  out->handle = static_cast<uint32_t>(r.handle);
  return true;
}

LoadResult PtraceSprxRuntime::load(pid_t pid, std::string_view path,
                                  std::string_view name, const LoadOptions &options) noexcept {
  static std::mutex mutex; std::lock_guard<std::mutex> lock(mutex);
  if(pid <= 1 || path.empty() || name.empty() || name.size() >= 256 ||
     !options.timeout_ms || !options.poll_ms || options.poll_ms > options.timeout_ms)
    return result_for(LoadStatus::InvalidArgument, pid);
  GameIdentity id; Receipt receipt;
  if(!capture_game(pid, id)) return result_for(LoadStatus::TargetNotFound, pid);
  std::string version, metadata_hash;
  if(!game_version(id,version,metadata_hash) ||
     (!options.game_version.empty() && version != options.game_version))
    return result_for(LoadStatus::PolicyDenied,pid);
  const int prior = read_receipt(id, name, receipt);
  if(prior != 0) {
    if(prior == 1) (void)refresh_completion(id,name,receipt);
    if(prior == 1 && metadata_hash == receipt.metadata_hash && receipt.state == Started && receipt.handle >= 0 &&
       receipt.initializer >= 0 && receipt.activation >= 0) {
      LoadResult r = result_for(LoadStatus::AlreadyLoaded, pid);
      r.module_handle = static_cast<uint32_t>(receipt.handle); return r;
    }
    LOG_WARN("[plugins] prior module attempt held pid=%d name=%.*s; restart game before retry", pid, static_cast<int>(name.size()), name.data());
    return result_for(LoadStatus::RemoteError, pid, false);
  }
  Session session(pid);
  if(!session.ready()) return result_for(LoadStatus::AttachFailed, pid);
  const uint32_t sdk = host_sdk(pid);
  if(!session.detach()) return result_for(LoadStatus::AttachFailed, pid, false);
  std::string staged, hash, error;
  if(!stage_module(id, path, sdk, staged, hash, error)) {
    LOG_ERROR("[plugins] stage failed pid=%d: %s", pid, error.c_str());
    return result_for(LoadStatus::AccessPrepareFailed, pid);
  }
  const std::string content_key = "sha256:" + hash;
  const int content_prior = read_receipt(id, content_key, receipt);
  if(content_prior != 0) {
    if(content_prior == 1) (void)refresh_completion(id,content_key,receipt);
    if(content_prior == 1 && metadata_hash == receipt.metadata_hash && receipt.state == Started && receipt.handle >= 0 &&
       receipt.initializer >= 0 && receipt.activation >= 0 && save_receipt(id, name, receipt)) {
      LoadResult r = result_for(LoadStatus::AlreadyLoaded, pid);
      r.module_handle = static_cast<uint32_t>(receipt.handle); return r;
    }
    return result_for(LoadStatus::RemoteError, pid, false);
  }
  if(!attempt_slot_available(id)) return result_for(LoadStatus::RemoteError, pid, false);
  timeval boot{};
  if(!boot_time(boot) || !same_game(id)) return result_for(LoadStatus::TargetExited, pid);
  receipt = {}; receipt.pid = pid; receipt.seconds = id.seconds; receipt.microseconds = id.microseconds;
  receipt.boot_seconds = boot.tv_sec; receipt.boot_microseconds = boot.tv_usec;
  if(metadata_hash.size()!=64) return result_for(LoadStatus::PolicyDenied,pid);
  memcpy(receipt.metadata_hash,metadata_hash.c_str(),65);
  memcpy(receipt.name, name.data(), name.size());
  if(!save_receipt(id, content_key, receipt) || !save_receipt(id, name, receipt))
    return result_for(LoadStatus::WriteFailed, pid, false);

  Session launch(pid);
  if(!launch.ready() || !same_game(id)) return result_for(LoadStatus::AttachFailed, pid, false);
  std::string current_version,current_hash;
  if(!game_version(id,current_version,current_hash) || current_hash!=metadata_hash)
    return result_for(LoadStatus::PolicyDenied,pid,false);
  const intptr_t load = pt_resolve(pid, "wzvqT4UqKX8");
  const intptr_t create = pt_resolve(pid, "OxhIB8LB-PQ");
  const intptr_t resolve = pt_resolve(pid, "LwG8g3niqwA");
  const intptr_t detach = symbol(pid, "scePthreadDetach");
  if(!load || !create || !resolve || !detach) return result_for(LoadStatus::ResolveFailed, pid, false);
  intptr_t data = pt_mmap(pid, 0, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  intptr_t code = pt_mmap(pid, 0, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(data <= 0 || code <= 0) return result_for(LoadStatus::AllocationFailed, pid, false);
  HyperhenPluginWorker context{};
  context.load_start_module = load; context.dlsym = resolve; context.handle = -1;
  if(staged.size() >= sizeof(context.path)) return result_for(LoadStatus::InvalidPath, pid, false);
  memcpy(context.path, staged.c_str(), staged.size()+1);
  const size_t bytes = static_cast<size_t>(hyperhen_plugin_worker_end-hyperhen_plugin_worker_start);
  const char worker_name[] = "HyperHEN plugin";
  if(!bytes || bytes > kPage || pt_copyin(pid, &context, data, sizeof(context)) ||
     pt_copyin(pid, worker_name, data+0x800, sizeof(worker_name)) ||
     pt_copyin(pid, hyperhen_plugin_worker_start, code, bytes) ||
     pt_mprotect(pid, code, kPage, PROT_READ | PROT_EXEC))
    return result_for(LoadStatus::WriteFailed, pid, false);
  receipt.context=data;receipt.load=load;receipt.resolve=resolve;
  memcpy(receipt.content_key,content_key.c_str(),content_key.size()+1);
  memcpy(receipt.staged_path,staged.c_str(),staged.size()+1);
  if(!save_receipt(id,content_key,receipt) || !save_receipt(id,name,receipt))
    return result_for(LoadStatus::WriteFailed,pid,false);
  const long created = pt_call(pid, create, data+0x1000, 0, code, data, data+0x800, 0);
  if(created) return result_for(LoadStatus::ThreadCreateFailed, pid, false);
  uint64_t thread = 0;
  if(!pt_copyout(pid, data+0x1000, &thread, sizeof(thread)) && thread)
    (void)pt_call(pid, detach, thread, 0, 0, 0, 0, 0);
  if(!launch.detach()) return result_for(LoadStatus::AttachFailed, pid, false);
  /* Do not poll a module handle: it can appear before module_start returns.
   * Read the game-owned worker's explicit completion, without stopping it. */
  const auto deadline = std::chrono::steady_clock::now()+std::chrono::milliseconds(options.timeout_ms);
  bool finished = false;
  while(std::chrono::steady_clock::now() < deadline && same_game(id)) {
    if(!onion_proc_copyout(pid, data, &context, sizeof(context)) &&
       (context.state == HH_PLUGIN_STARTED || context.state == HH_PLUGIN_FAILED)) { finished = true; break; }
    usleep(std::min<uint32_t>(options.poll_ms, 1000)*1000);
  }
  if(!finished || !same_game(id)) {
    LOG_WARN("[plugins] initializer outcome unknown pid=%d; game restart required", pid);
    return result_for(same_game(id)?LoadStatus::Timeout:LoadStatus::TargetExited, pid, false);
  }
  receipt.handle = context.handle; receipt.initializer = context.start_result;
  receipt.activation = context.activation_result;
  receipt.state = context.state == HH_PLUGIN_STARTED ? Started : Failed;
  const bool saved = save_receipt(id, content_key, receipt) && save_receipt(id, name, receipt);
  LOG_INFO("[plugins] initialized pid=%d module=%.*s handle=%08x start=%08x activation=%08x extra=%u saved=%d",
           pid, static_cast<int>(name.size()), name.data(), static_cast<unsigned>(context.handle),
           static_cast<unsigned>(context.start_result), static_cast<unsigned>(context.activation_result), context.activated, saved);
  const bool success = saved && context.state == HH_PLUGIN_STARTED;
  LoadResult result = result_for(success ? LoadStatus::Loaded : LoadStatus::RemoteError, pid, success);
  if(result.succeeded()) result.module_handle = static_cast<uint32_t>(context.handle);
  result.remote_result = context.start_result;
  /* The worker's final store precedes its return. Retain its RX/RW pages until
   * the game exits, so no executing epilogue can be unmapped. At most one
   * worker per declared module/process instance; unknown attempts never retry. */
  return result;
}
}
