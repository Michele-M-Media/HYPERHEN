/* SPDX-License-Identifier: GPL-3.0-or-later
 * Sandbox/staging workflow adapted from TacHen's game-source.c and
 * module-storage.c. Original notices: third_party/tachen-plugin-runtime.
 */
#include <onion/plugin_staging.hpp>
#include <onion/log.h>
#include <onion/plugin_metadata.hpp>
#include "prx-image.h"
#include "process-info.h"
#include "game-info.h"
#include "game-source.h"
extern "C" {
#include "sha256.h"
}
#include <ps5/kernel.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

extern "C" int sceKernelGetAppInfo(int, void *);
namespace onion::sprx {
namespace {
struct Fd {
  int value = -1;
  explicit Fd(int fd = -1) : value(fd) {}
  ~Fd() { if(value >= 0) close(value); }
};
bool title_for(pid_t pid, std::string &title) {
  alignas(8) unsigned char app[128]{};
  if(sceKernelGetAppInfo(pid, app)) return false;
  HhpAppInfo decoded{};
  if(hhp_app_info_decode(app, 96, &decoded)) return false;
  title = decoded.title;
  return true;
}
bool transfer(int fd, void *data, size_t size, bool writing) {
  auto *p = static_cast<unsigned char *>(data);
  while(size) {
    ssize_t n = writing ? write(fd, p, size) : read(fd, p, size);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) return false;
    p += n; size -= static_cast<size_t>(n);
  }
  return true;
}
int child(int fd, const char *name, mode_t mode) {
  if(mkdirat(fd, name, mode) && errno != EEXIST) return -1;
  int out = openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  struct stat st{};
  if(out < 0) return -1;
  if(fstat(out, &st) || !S_ISDIR(st.st_mode) || (st.st_mode & 0022) ||
     (st.st_uid != 0 && st.st_uid != getuid()) || fchmod(out, mode)) {
    close(out); return -1;
  }
  return out;
}
int sandbox(const GameIdentity &id, std::string &path, bool &global) {
  if(!same_game(id)) return -1;
  intptr_t original = kernel_get_proc_rootdir(id.pid);
  intptr_t expected = original, root = kernel_get_root_vnode();
  if(!original || !root) return -1;
  if(expected == root) {
    intptr_t jail = kernel_get_proc_jaildir(id.pid);
    if(jail && jail != root) expected = jail;
  }
  const bool lookup_global = expected == root;
  global = original == root;
  DIR *dir = opendir("/mnt/sandbox");
  if(!dir) return -1;
  int selected = -1;
  unsigned count = 0;
  while(dirent *entry = readdir(dir)) {
    if(++count > 4096) { if(selected >= 0) close(selected); selected = -1; break; }
    std::string_view name(entry->d_name);
    if(!name.starts_with(id.title + "_") || name.size() < 11 || name.size() > 16)
      continue;
    if(!std::all_of(name.begin()+10, name.end(), [](char c){return c>='0' && c<='9';}))
      continue;
    int candidate = openat(dirfd(dir), entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if(candidate < 0) continue;
    if(!lookup_global && kernel_get_proc_file(getpid(), candidate) != expected) {
      close(candidate); continue;
    }
    if(lookup_global) {
      Fd app(openat(candidate, "app0", O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
      if(app.value < 0) { close(candidate); continue; }
      if(selected >= 0) { close(candidate); close(selected); selected = -1; break; }
    }
    selected = candidate; path = "/mnt/sandbox/" + std::string(name);
    if(!lookup_global) break;
  }
  closedir(dir);
  if(selected >= 0 && (!same_game(id) || kernel_get_proc_rootdir(id.pid) != original)) {
    close(selected); return -1;
  }
  return selected;
}
}
bool capture_game(pid_t pid, GameIdentity &id) noexcept {
  kinfo_proc process{};
  std::string title;
  if(pid <= 1 || hhp_process_info(pid, &process) || process.ki_start.tv_sec <= 0 ||
     process.ki_start.tv_usec < 0 || process.ki_start.tv_usec >= 1000000 ||
     !title_for(pid, title)) return false;
  id = {pid, process.ki_start.tv_sec, process.ki_start.tv_usec, std::move(title)};
  id.rootdir = kernel_get_proc_rootdir(pid); id.jaildir = kernel_get_proc_jaildir(pid);
  if(!id.rootdir) return false;
  return true;
}
bool same_game(const GameIdentity &id) noexcept {
  GameIdentity now;
  return capture_game(id.pid, now) && id.seconds == now.seconds &&
         id.microseconds == now.microseconds && id.title == now.title &&
         id.rootdir == now.rootdir && id.jaildir == now.jaildir;
}
bool game_version(const GameIdentity &id, std::string &version, std::string &hash) noexcept {
  timeval boot{}; size_t boot_size=sizeof(boot);int mib[]={CTL_KERN,KERN_BOOTTIME};
  if(sysctl(mib,2,&boot,&boot_size,nullptr,0) || boot_size!=sizeof(boot) || !same_game(id)) return false;
  char generation[128];snprintf(generation,sizeof(generation),"%lld:%lld/%d/%lld:%lld",
    static_cast<long long>(boot.tv_sec),static_cast<long long>(boot.tv_usec),id.pid,
    static_cast<long long>(id.seconds),static_cast<long long>(id.microseconds));
  unsigned char *bytes=nullptr;size_t size=0;char reason[192]{};
  if(hhp_game_metadata_read(id.pid,generation,id.title.c_str(),&bytes,&size,reason,sizeof(reason))) {
    LOG_WARN("[plugins] game metadata unavailable pid=%d: %s",id.pid,reason);return false;
  }
  const bool ok=parse_game_metadata({bytes,size},id.title,version);
  if(ok) {
    SHA256_CTX sha;unsigned char digest[32];char hex[65]{};
    sha256_init(&sha);sha256_update(&sha,bytes,size);sha256_final(&sha,digest);
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);hash=hex;
  }
  free(bytes);return ok && same_game(id);
}
bool stage_module(const GameIdentity &id, std::string_view source,
                  uint32_t sdk, std::string &path, std::string &hash,
                  std::string &error) noexcept {
  error = "plugin staging failed";
  const std::string filename(source);
  Fd input(open(filename.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK));
  struct stat before{}, after{};
  if(input.value < 0 || fstat(input.value, &before) || !S_ISREG(before.st_mode) ||
     before.st_size < 64 || before.st_size > 128 * 1024 * 1024) return false;
  std::vector<unsigned char> bytes(static_cast<size_t>(before.st_size));
  if(!transfer(input.value, bytes.data(), bytes.size(), false) ||
     fstat(input.value, &after) || after.st_size != before.st_size ||
     after.st_mtim.tv_sec != before.st_mtim.tv_sec || after.st_mtim.tv_nsec != before.st_mtim.tv_nsec ||
     after.st_ctim.tv_sec != before.st_ctim.tv_sec || after.st_ctim.tv_nsec != before.st_ctim.tv_nsec)
    return false;
  int platform = 0;
  char reason[192]{};
  if(hhp_prx_inspect(bytes.data(), bytes.size(), &platform, reason, sizeof(reason))) {
    error = reason; return false;
  }
  const bool ps5 = id.title.starts_with("PPS") || id.title.starts_with("MOUU");
  if(platform && platform != (ps5 ? 5 : 4)) { error = "plugin targets a different console platform"; return false; }
  uint32_t original_sdk = 0;
  int adapted = ps5 ? hhp_prx_match_host_sdk(bytes.data(), bytes.size(), sdk,
                                            kernel_get_fw_version(), &original_sdk) : 0;
  if(adapted < 0) { error = "invalid development module SDK metadata"; return false; }
  unsigned char digest[32];
  SHA256_CTX sha; sha256_init(&sha); sha256_update(&sha, bytes.data(), bytes.size()); sha256_final(&sha, digest);
  char text[65]{};
  for(unsigned i=0; i<32; ++i) snprintf(text+i*2, 3, "%02x", digest[i]);
  hash = text;
  char variant[40]; snprintf(variant, sizeof(variant), "v1-p%d-%08x", ps5?5:4, sdk);
  Fd home(open("/data/HyperHEN", O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  if(home.value < 0) return false;
  Fd cache(child(home.value, "plugin-modules", 0700));
  if(cache.value < 0) return false;
  Fd backing(child(cache.value, variant, 0755));
  if(backing.value < 0) return false;
  std::string root_path; bool global = false;
  Fd game(sandbox(id, root_path, global));
  if(game.value < 0) { error = "verified running game sandbox is not ready"; return false; }
  if(mkdirat(game.value, "hyperhen-modules", 0755) && errno != EEXIST) return false;
  Fd target(openat(game.value, "hyperhen-modules", O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  struct statfs fs{};
  if(target.value < 0 || fstatfs(target.value, &fs)) return false;
  const std::string from = "/data/HyperHEN/plugin-modules/" + std::string(variant);
  const std::string to = root_path + "/hyperhen-modules";
  if(to != fs.f_mntonname) {
    if(!same_game(id)) return false;
    const char *v[] = {"fstype", "nullfs", "from", from.c_str(), "fspath", to.c_str()};
    iovec io[6];
    for(unsigned i=0;i<6;++i) { io[i].iov_base = const_cast<char *>(v[i]); io[i].iov_len = strlen(v[i])+1; }
    if(syscall(SYS_nmount, io, 6, MNT_RDONLY)) {
      error = "cannot mount read-only executable game plugin storage"; return false;
    }
    close(target.value); target.value = openat(game.value, "hyperhen-modules", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if(target.value < 0 || fstatfs(target.value, &fs)) return false;
  }
  if(strcmp(fs.f_fstypename, "nullfs") || from != fs.f_mntfromname || to != fs.f_mntonname ||
     !(fs.f_flags & MNT_RDONLY) || (fs.f_flags & MNT_NOEXEC) || !same_game(id)) {
    error = "game plugin mount identity or permissions do not match"; return false;
  }
  const std::string suffix = filename.ends_with(".sprx") ? ".sprx" : ".prx";
  const std::string final = hash + suffix;
  Fd existing(openat(backing.value, final.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK));
  if(existing.value >= 0) {
    struct stat old{};
    if(fstat(existing.value, &old) || !S_ISREG(old.st_mode) || old.st_size != before.st_size ||
       (old.st_mode & 0222)) return false;
    std::vector<unsigned char> saved(bytes.size());
    if(!transfer(existing.value, saved.data(), saved.size(), false) || saved != bytes ||
       fchmod(existing.value, 0555)) return false;
  } else {
    if(errno != ENOENT) return false;
    char temp[80]; snprintf(temp, sizeof(temp), ".%s-%08x.part", hash.c_str(), arc4random());
    Fd output(openat(backing.value, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600));
    if(output.value < 0) return false;
    bool ok = transfer(output.value, bytes.data(), bytes.size(), true) &&
              fchmod(output.value, 0555) == 0 && fsync(output.value) == 0 &&
              renameat(backing.value, temp, backing.value, final.c_str()) == 0 &&
              fsync(backing.value) == 0;
    if(!ok) { unlinkat(backing.value, temp, 0); return false; }
  }
  /* A game whose root was deliberately restored to the global root resolves
   * the verified sandbox's full path, not a nonexistent global /hyperhen-modules. */
  path = (global ? to : "/hyperhen-modules") + "/" + final;
  LOG_INFO("[plugins] staged pid=%d title=%s sdk=%08x module-sdk=%08x adapted=%d sha256=%s",
           id.pid, id.title.c_str(), sdk, original_sdk, adapted, hash.c_str());
  error.clear();
  return same_game(id);
}
}
