#include "hyper_installer_engine.hpp"
#include "hyper_installer.hpp"
#include "hyper_install_lease.hpp"
#include <onion/hyper_assets.hpp>
extern "C" {
#include <sha256.h>
}
#include <miniz.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

namespace onion::hyper {
namespace {
using onion::cheats::sync::HttpRequest;
using onion::cheats::sync::SyncStatus;
constexpr uint64_t kFreeReserve = 32ull * 1024 * 1024;

bool cancelled(const InstallInputs &in) {
  return in.cancelled && in.cancelled(in.user);
}
bool safe_relative(const char *value) {
  if (!value || !*value) return false;
  const std::string path(value);
  if (path.size() > 240 || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
    return false;
  size_t from = 0;
  do {
    const size_t end = path.find('/', from);
    const std::string part = path.substr(from, end == std::string::npos ? end : end - from);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) break;
    from = end + 1;
  } while (from < path.size());
  return true;
}
bool directory(const std::string &path, bool create) {
  if (path.empty() || path.front() != '/' || path.find("/../") != std::string::npos)
    return false;
  size_t from = 1;
  while (from <= path.size()) {
    const size_t end = path.find('/', from);
    const std::string prefix = path.substr(0, end);
    struct stat st {};
    if (lstat(prefix.c_str(), &st) != 0) {
      if (!create || errno != ENOENT || mkdir(prefix.c_str(), 0755) != 0) return false;
    } else if (!S_ISDIR(st.st_mode)) return false;
    if (end == std::string::npos) break;
    from = end + 1;
  }
  return true;
}
bool parent(const std::string &path, bool create) {
  return directory(path.substr(0, path.find_last_of('/')), create);
}
bool absent_or_regular(const std::string &path) {
  struct stat st {};
  return lstat(path.c_str(), &st) == 0 ? S_ISREG(st.st_mode) : errno == ENOENT;
}
bool write_all(int fd, const void *bytes, size_t size) {
  auto *p = static_cast<const unsigned char *>(bytes);
  while (size) {
    const ssize_t n = write(fd, p, size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    p += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}
void sync_dir(const std::string &path) {
  const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (fd >= 0) { (void)fsync(fd); (void)close(fd); }
}
bool marker(const std::string &path, const char *bytes, size_t size) {
  const std::string temp = path + ".part";
  if (!absent_or_regular(path) || !absent_or_regular(temp)) return false;
  const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
  if (fd < 0) return false;
  const bool ok = write_all(fd, bytes, size) && fsync(fd) == 0;
  const int close_rc = close(fd);
  if (!ok || close_rc != 0 || rename(temp.c_str(), path.c_str()) != 0) {
    (void)unlink(temp.c_str());
    return false;
  }
  sync_dir(path.substr(0, path.find_last_of('/')));
  return true;
}
void digest_hex(const unsigned char *digest, char hex[65]) {
  static constexpr char chars[] = "0123456789abcdef";
  for (size_t n = 0; n < 32; ++n) {
    hex[n * 2] = chars[digest[n] >> 4];
    hex[n * 2 + 1] = chars[digest[n] & 15];
  }
  hex[64] = 0;
}
bool memory_sha(const unsigned char *bytes, size_t size, const char *expected) {
  SHA256_CTX sha;
  unsigned char digest[32]; char hex[65];
  sha256_init(&sha); sha256_update(&sha, bytes, size); sha256_final(&sha, digest);
  digest_hex(digest, hex);
  return expected && std::strcmp(hex, expected) == 0;
}
enum class FileCheck { Valid, Missing, Invalid, Io, Cancelled, Unsafe };
FileCheck verify_file(const InstallInputs &in, const std::string &path,
                      const AssetFile &spec) {
  if (cancelled(in)) return FileCheck::Cancelled;
  struct stat st {};
  if (lstat(path.c_str(), &st) != 0) return errno == ENOENT ? FileCheck::Missing : FileCheck::Io;
  if (!S_ISREG(st.st_mode) || !parent(path, false)) return FileCheck::Unsafe;
  if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) != spec.size) return FileCheck::Invalid;
  const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return FileCheck::Io;
  SHA256_CTX sha; sha256_init(&sha);
  constexpr size_t kHashBlockBytes = 64u * 1024u;
  std::unique_ptr<unsigned char[]> buffer(new (std::nothrow) unsigned char[kHashBlockBytes]);
  if (!buffer) { (void)close(fd); return FileCheck::Io; }
  uint64_t read_bytes = 0;
  FileCheck result = FileCheck::Valid;
  for (;;) {
    if (cancelled(in)) { result = FileCheck::Cancelled; break; }
    const ssize_t n = read(fd, buffer.get(), kHashBlockBytes);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { result = FileCheck::Io; break; }
    if (!n) break;
    read_bytes += static_cast<uint64_t>(n);
    if (read_bytes > spec.size) { result = FileCheck::Invalid; break; }
    sha256_update(&sha, buffer.get(), static_cast<size_t>(n));
  }
  if (close(fd) != 0 && result == FileCheck::Valid) result = FileCheck::Io;
  if (result != FileCheck::Valid) return result;
  unsigned char digest[32]; char hex[65];
  sha256_final(&sha, digest); digest_hex(digest, hex);
  return read_bytes == spec.size && std::strcmp(hex, spec.sha256) == 0 ?
      FileCheck::Valid : FileCheck::Invalid;
}
SyncStatus check_error(FileCheck check) {
  if (check == FileCheck::Cancelled) return SyncStatus::Cancelled;
  if (check == FileCheck::Unsafe) return SyncStatus::Rejected;
  return SyncStatus::Io;
}
struct Progress {
  const InstallInputs &in;
  uint64_t complete = 0;
  uint64_t total = 0;
  void report(const char *state, uint64_t current = 0) const {
    if (in.progress) in.progress(state, std::min(total, complete + current), total, in.user);
  }
};
struct DownloadProgress { Progress *progress; uint64_t offset; };
bool safe_marker_name(const char *name) {
  return name && name[0] == '/' && name[1] == '.' && name[2] &&
         std::strlen(name) < 64 && !std::strchr(name + 1, '/') &&
         !std::strchr(name, '\\') && std::strcmp(name, kInstallingName) != 0;
}
bool erase_marker(const std::string &root, const char *name) {
  if (!name) return true;
  const std::string path = root + name;
  return absent_or_regular(path) && (unlink(path.c_str()) == 0 || errno == ENOENT);
}
bool invalidate_ready(const InstallInputs &in, const std::string &root, bool common) {
  if (common && in.base_ready_name)
    return erase_marker(root, in.base_ready_name) && erase_marker(root, kReadyName);
  return erase_marker(root, in.ready_name) && erase_marker(root, in.compat_ready_name);
}
void download_progress(size_t now, size_t, void *user) {
  const auto *p = static_cast<DownloadProgress *>(user);
  p->progress->report("downloading", p->offset + now);
}
SyncStatus download_model(const InstallInputs &in, const ModelFile &model,
                           const std::string &root, Progress &progress, bool common) {
  const std::string dest = root + "/models/" + model.file.path;
  if (!parent(dest, true) || !absent_or_regular(dest)) return SyncStatus::Rejected;
  progress.report("verifying");
  FileCheck checked = verify_file(in, dest, model.file);
  if (checked == FileCheck::Valid) { progress.complete += model.file.size; return SyncStatus::Ok; }
  if (checked != FileCheck::Missing && checked != FileCheck::Invalid) return check_error(checked);
  if (!invalidate_ready(in, root, common)) return SyncStatus::Io;
  if (!in.allow_download) return SyncStatus::Protocol;
  const std::string part = root + "/.install-staging/" + model.file.path + ".part";
  if (!parent(part, true) || !absent_or_regular(part)) return SyncStatus::Rejected;
  struct stat st {};
  uint64_t offset = 0;
  if (lstat(part.c_str(), &st) == 0) {
    if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) > model.file.size) {
      if (unlink(part.c_str()) != 0) return SyncStatus::Io;
    } else offset = static_cast<uint64_t>(st.st_size);
  }
  for (int attempt = 0; offset < model.file.size && attempt < 2; ++attempt) {
    if (cancelled(in)) return SyncStatus::Cancelled;
    struct statvfs space {};
    if (statvfs(root.c_str(), &space) != 0) return SyncStatus::Io;
    if (static_cast<uint64_t>(space.f_bavail) * space.f_frsize <
        model.file.size - offset + kFreeReserve) return SyncStatus::NoSpace;
    const int fd = open(part.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW, 0644);
    if (fd < 0 || lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
      if (fd >= 0) (void)close(fd);
      return SyncStatus::Io;
    }
    bool range_ignored = false;
    DownloadProgress bridge{&progress, offset};
    HttpRequest req;
    req.url = model.url; req.host_allow = "huggingface.co";
    req.https_only = true; req.resume_from_bytes = offset;
    req.range_ignored = &range_ignored; req.timeout_ms = 0;
    req.status_min = req.status_max = offset ? 206 : 200;
    req.max_body_bytes = static_cast<size_t>(model.file.size - offset);
    req.on_progress = download_progress; req.progress_user = &bridge;
    req.should_cancel = in.cancelled; req.cancel_user = in.user;
    uint64_t written = offset;
    progress.report("downloading", offset);
    SyncStatus result = in.http->perform(req, [&](const void *data, size_t size) {
      if (cancelled(in)) return SyncStatus::Cancelled;
      if (written > model.file.size || size > model.file.size - written) return SyncStatus::Protocol;
      if (!write_all(fd, data, size)) return errno == ENOSPC ? SyncStatus::NoSpace : SyncStatus::Io;
      written += size;
      return SyncStatus::Ok;
    });
    const bool flushed = fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    if (!flushed || !closed) return SyncStatus::Io;
    if (range_ignored && offset) {
      // Transport rejects 200 before on_data: restart from zero, never append it.
      if (unlink(part.c_str()) != 0) return SyncStatus::Io;
      offset = 0;
      continue;
    }
    if (result != SyncStatus::Ok) return result;
    if (written != model.file.size) return SyncStatus::Network;
    offset = written;
  }
  // Keep downloaded bytes visible while hashing the completed staging file.
  progress.report("verifying", model.file.size);
  checked = verify_file(in, part, model.file);
  if (checked != FileCheck::Valid) {
    if (checked == FileCheck::Invalid) { (void)unlink(part.c_str()); return SyncStatus::Protocol; }
    return check_error(checked);
  }
  if (cancelled(in)) return SyncStatus::Cancelled;
  if (rename(part.c_str(), dest.c_str()) != 0) return SyncStatus::Io;
  sync_dir(root + "/models");
  progress.complete += model.file.size;
  progress.report("installing");
  return SyncStatus::Ok;
}
SyncStatus verify_voice(const InstallInputs &in, const std::string &voice_root) {
  if (!directory(voice_root, false)) {
    struct stat st {};
    if (lstat(voice_root.c_str(), &st) != 0 && errno == ENOENT) return SyncStatus::Protocol;
    return SyncStatus::Rejected;
  }
  for (size_t i = 0; i < in.voice_count; ++i) {
    const auto check = verify_file(in, voice_root + '/' + in.voices[i].path, in.voices[i]);
    if (check == FileCheck::Missing || check == FileCheck::Invalid) return SyncStatus::Protocol;
    if (check != FileCheck::Valid) return check_error(check);
  }
  return SyncStatus::Ok;
}
bool remove_voice_stage(const InstallInputs &in, const std::string &root) {
  struct stat st {};
  if (lstat(root.c_str(), &st) != 0) return errno == ENOENT;
  if (!directory(root, false)) return false;
  std::set<std::string> dirs;
  // Only remove paths from our trusted manifest; never recursively erase unknown data.
  for (size_t i = 0; i < in.voice_count; ++i) {
    const std::string path = root + '/' + in.voices[i].path;
    if (!parent(path, false)) {
      const std::string parent_path = path.substr(0, path.find_last_of('/'));
      if (lstat(parent_path.c_str(), &st) == 0) return false;
    } else if (!absent_or_regular(path)) return false;
    else if (unlink(path.c_str()) != 0 && errno != ENOENT) return false;
    std::string dir = path.substr(0, path.find_last_of('/'));
    while (dir.size() > root.size()) {
      dirs.insert(dir); dir = dir.substr(0, dir.find_last_of('/'));
    }
  }
  for (auto d = dirs.rbegin(); d != dirs.rend(); ++d)
    if (rmdir(d->c_str()) != 0 && errno != ENOENT) return false;
  return rmdir(root.c_str()) == 0 || errno == ENOENT;
}
SyncStatus install_voice(const InstallInputs &in, const std::string &root, Progress &progress) {
  uint64_t voice_bytes = 0;
  for (size_t i = 0; i < in.voice_count; ++i) voice_bytes += in.voices[i].size;
  const std::string dest = root + "/espeak-ng-data";
  progress.report("verifying");
  SyncStatus valid = verify_voice(in, dest);
  if (valid == SyncStatus::Ok) { progress.complete += voice_bytes; return valid; }
  if (valid != SyncStatus::Protocol) return valid;
  if (!invalidate_ready(in, root, true)) return SyncStatus::Io;
  if (!in.allow_download) return SyncStatus::Protocol;
  struct statvfs space {};
  if (statvfs(root.c_str(), &space) != 0) return SyncStatus::Io;
  if (static_cast<uint64_t>(space.f_bavail) * space.f_frsize < voice_bytes + kFreeReserve)
    return SyncStatus::NoSpace;
  const std::string stage = root + "/.install-staging/espeak-ng-data";
  const std::string previous = root + "/.install-staging/voice-previous";
  if (!remove_voice_stage(in, stage) || !directory(stage, true)) return SyncStatus::Io;
  if (!memory_sha(in.voice_zip, in.voice_zip_size, in.voice_zip_sha256)) return SyncStatus::Protocol;
  mz_zip_archive zip {};
  if (!mz_zip_reader_init_mem(&zip, in.voice_zip, in.voice_zip_size, 0)) return SyncStatus::Protocol;
  struct ZipClose { mz_zip_archive *zip; ~ZipClose() { mz_zip_reader_end(zip); } } close_zip{&zip};
  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  if (count != in.voice_count) return SyncStatus::Protocol;
  std::vector<mz_uint> indices(in.voice_count, count);
  // Reject every unexpected/duplicate/directory/symlink entry before writing.
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat entry {};
    if (!mz_zip_reader_file_stat(&zip, i, &entry) || !safe_relative(entry.m_filename) ||
        entry.m_is_directory || !entry.m_is_supported || entry.m_is_encrypted ||
        ((entry.m_external_attr >> 16) & S_IFMT) != S_IFREG) return SyncStatus::Protocol;
    size_t match = in.voice_count;
    for (size_t j = 0; j < in.voice_count; ++j)
      if (std::strcmp(in.voices[j].path, entry.m_filename) == 0) { match = j; break; }
    if (match == in.voice_count || indices[match] != count ||
        entry.m_uncomp_size != in.voices[match].size) return SyncStatus::Protocol;
    indices[match] = i;
  }
  uint64_t extracted = 0;
  for (size_t i = 0; i < in.voice_count; ++i) {
    if (cancelled(in)) return SyncStatus::Cancelled;
    const std::string path = stage + '/' + in.voices[i].path;
    if (!parent(path, true) || !absent_or_regular(path)) return SyncStatus::Rejected;
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) return SyncStatus::Io;
    struct ExtractWriter { int fd; const InstallInputs *in; uint64_t size; uint64_t expected; } writer{fd, &in, 0, in.voices[i].size};
    auto write_cb = [](void *p, mz_uint64 offset, const void *bytes, size_t size) -> size_t {
      auto *w = static_cast<ExtractWriter *>(p);
      if (cancelled(*w->in) || offset != w->size || w->size > w->expected ||
          size > w->expected - w->size || !write_all(w->fd, bytes, size)) return 0;
      w->size += size; return size;
    };
    const bool ok = mz_zip_reader_extract_to_callback(&zip, indices[i], write_cb, &writer, 0) &&
                    writer.size == writer.expected;
    const bool synced = fsync(fd) == 0; const bool closed = close(fd) == 0;
    if (cancelled(in)) return SyncStatus::Cancelled;
    if (!ok || !synced || !closed) return SyncStatus::Io;
    const FileCheck checked = verify_file(in, path, in.voices[i]);
    if (checked != FileCheck::Valid) return checked == FileCheck::Invalid ? SyncStatus::Protocol : check_error(checked);
    extracted += in.voices[i].size;
    progress.report("installing", extracted);
  }
  if (cancelled(in)) return SyncStatus::Cancelled;
  if (!remove_voice_stage(in, previous)) return SyncStatus::Io;
  struct stat st {};
  const bool had_dest = lstat(dest.c_str(), &st) == 0;
  if (had_dest && (!directory(dest, false) || rename(dest.c_str(), previous.c_str()) != 0)) return SyncStatus::Io;
  if (rename(stage.c_str(), dest.c_str()) != 0) {
    if (had_dest) (void)rename(previous.c_str(), dest.c_str());
    return SyncStatus::Io;
  }
  sync_dir(root);
  // Backups containing unknown user files stay untouched; never risk recursive deletion.
  (void)remove_voice_stage(in, previous);
  progress.complete += voice_bytes;
  return SyncStatus::Ok;
}
} // namespace

SyncStatus install_assets(const InstallInputs &in) {
  if (!in.root || !*in.root || !in.models || !in.model_count || in.model_count > 8 ||
      !in.voices || !in.voice_count || in.voice_count > 1024 ||
      (in.allow_download && (!in.voice_zip || !in.voice_zip_size ||
       !in.voice_zip_sha256 || !in.http))) return SyncStatus::Rejected;
  if (!safe_marker_name(in.ready_name) || !in.ready_version ||
      !std::strlen(in.ready_version) || std::strlen(in.ready_version) >= 128 ||
      in.common_model_count > in.model_count ||
      (in.base_ready_name && (!safe_marker_name(in.base_ready_name) ||
       !in.base_ready_version || !std::strlen(in.base_ready_version) ||
       std::strlen(in.base_ready_version) >= 128)) ||
      (in.compat_ready_name && !safe_marker_name(in.compat_ready_name))) return SyncStatus::Rejected;
  Progress progress{in, 0, 0};
  std::set<std::string> paths;
  for (size_t i = 0; i < in.model_count; ++i) {
    const auto &m = in.models[i];
    if (!safe_relative(m.file.path) || std::strchr(m.file.path, '/') || !m.file.size ||
        !m.file.sha256 || std::strlen(m.file.sha256) != 64 || !m.url ||
        std::strncmp(m.url, "https://huggingface.co/", 23) != 0 || !paths.insert(m.file.path).second)
      return SyncStatus::Rejected;
    progress.total += m.file.size;
  }
  paths.clear();
  for (size_t i = 0; i < in.voice_count; ++i) {
    const auto &v = in.voices[i];
    if (!safe_relative(v.path) || !v.sha256 || std::strlen(v.sha256) != 64 ||
        !paths.insert(v.path).second) return SyncStatus::Rejected;
    progress.total += v.size;
  }
  const std::string root(in.root);
  if (!directory(root, true)) return SyncStatus::Rejected;
  install_detail::ExclusiveLease installer_lease;
  const auto lease_result = installer_lease.acquire(root + install_detail::kInstallerLeaseName);
  if (lease_result == install_detail::LockResult::Busy) return SyncStatus::Busy;
  if (lease_result != install_detail::LockResult::Acquired)
    return lease_result == install_detail::LockResult::Unsafe ? SyncStatus::Rejected : SyncStatus::Io;
  if (!directory(root + "/.install-staging", true)) return SyncStatus::Rejected;
  const std::string busy = root + kInstallingName;
  const std::string ready = root + in.ready_name;
  const auto old_marker = install_detail::known_marker(busy, kAssetVersion);
  if (old_marker == install_detail::MarkerResult::Unsafe) return SyncStatus::Rejected;
  if (old_marker == install_detail::MarkerResult::Io) return SyncStatus::Io;
  if (!absent_or_regular(ready)) return SyncStatus::Rejected;
  if ((in.base_ready_name && !absent_or_regular(root + in.base_ready_name)) ||
      (in.compat_ready_name && !absent_or_regular(root + in.compat_ready_name))) return SyncStatus::Rejected;
  if (!marker(busy, kAssetVersion, sizeof(kAssetVersion) - 1)) return SyncStatus::Io;
  struct stat busy_identity{};
  if (install_detail::known_marker(busy, kAssetVersion, &busy_identity) !=
      install_detail::MarkerResult::Known) return SyncStatus::Io;
  struct BusyGuard {
    std::string path;
    struct stat identity;
    ~BusyGuard() { (void)install_detail::remove_known_marker(path, kAssetVersion, identity); }
  } guard{busy, busy_identity};
  // .installing gates lifecycle while existing verified readiness survives
  // cancel/network errors. A marker is invalidated only after detecting its
  // specific existing asset set is missing/corrupt.
  if (in.quiesce) {
    const SyncStatus stopped = in.quiesce(in.user);
    if (stopped != SyncStatus::Ok) return stopped;
  }
  // Each actual download/extraction checks its remaining space requirement.
  // Valid existing/manual assets require only the tiny durable marker writes.
  for (size_t i = 0; i < in.model_count; ++i) {
    const SyncStatus result = download_model(in, in.models[i], root, progress,
        i >= in.model_count - in.common_model_count);
    if (result != SyncStatus::Ok) return result;
  }
  const SyncStatus voice = install_voice(in, root, progress);
  if (voice != SyncStatus::Ok) return voice;
  if (cancelled(in)) return SyncStatus::Cancelled;
  if (in.base_ready_name && !marker(root + in.base_ready_name, in.base_ready_version,
                                  std::strlen(in.base_ready_version))) return SyncStatus::Io;
  if (!marker(ready, in.ready_version, std::strlen(in.ready_version))) return SyncStatus::Io;
  if (in.compat_ready_name && !marker(root + in.compat_ready_name, kAssetVersion,
                                      sizeof(kAssetVersion) - 1)) return SyncStatus::Io;
  progress.report("ready");
  return SyncStatus::Ok;
}

InstallRecoveryResult recover_interrupted_install(const char *root,
                                                  const char *worker_lock) {
  using namespace install_detail;
  if (!root || !worker_lock) return InstallRecoveryResult::Unsafe;
  const std::string base(root);
  if (!safe_directory(base)) {
    if (errno == ENOENT) return InstallRecoveryResult::NoMarker;
    return errno == EINVAL || errno == ELOOP || errno == ENOTDIR ?
        InstallRecoveryResult::Unsafe : InstallRecoveryResult::Io;
  }
  const std::string marker_path = base + kInstallingName;
  auto marker_result = known_marker(marker_path, kAssetVersion);
  if (marker_result == MarkerResult::Missing) return InstallRecoveryResult::NoMarker;
  if (marker_result != MarkerResult::Known)
    return marker_result == MarkerResult::Unsafe ? InstallRecoveryResult::Unsafe : InstallRecoveryResult::Io;

  ExclusiveLease installer_lease;
  const auto installer = installer_lease.acquire(base + kInstallerLeaseName);
  if (installer == LockResult::Busy) return InstallRecoveryResult::InstallerBusy;
  if (installer != LockResult::Acquired)
    return installer == LockResult::Unsafe ? InstallRecoveryResult::Unsafe : InstallRecoveryResult::Io;
  // A job that completed while recovery was acquiring its lease has already
  // removed the marker. Never infer an abandoned job from an earlier stat.
  struct stat marker_identity{};
  marker_result = known_marker(marker_path, kAssetVersion, &marker_identity);
  if (marker_result == MarkerResult::Missing) return InstallRecoveryResult::NoMarker;
  if (marker_result != MarkerResult::Known)
    return marker_result == MarkerResult::Unsafe ? InstallRecoveryResult::Unsafe : InstallRecoveryResult::Io;

  ExclusiveLease worker_lease;
  const auto worker = worker_lease.acquire(worker_lock);
  if (worker == LockResult::Busy) return InstallRecoveryResult::WorkerBusy;
  if (worker != LockResult::Acquired)
    return worker == LockResult::Unsafe ? InstallRecoveryResult::Unsafe : InstallRecoveryResult::Io;
  if (!remove_known_marker(marker_path, kAssetVersion, marker_identity))
    return InstallRecoveryResult::Io;
  sync_dir(base);
  // Both RAII guards release their flock before control returns to lifecycle.
  return InstallRecoveryResult::Recovered;
}
} // namespace onion::hyper
