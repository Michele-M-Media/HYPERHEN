#pragma once

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::hyper::install_detail {
inline constexpr char kInstallerLeaseName[] = "/.installer.lock";
enum class LockResult { Acquired, Busy, Unsafe, Io };
enum class MarkerResult { Known, Missing, Unsafe, Io };

inline bool safe_directory(const std::string &path) {
  if (path.empty() || path.front() != '/' || path.back() == '/') {
    errno = EINVAL;
    return false;
  }
  size_t begin = 1;
  while (begin < path.size()) {
    const size_t end = path.find('/', begin);
    const std::string part = path.substr(begin, end == std::string::npos ? end : end - begin);
    if (part.empty() || part == "." || part == "..") { errno = EINVAL; return false; }
    struct stat file{};
    if (lstat(path.substr(0, end).c_str(), &file) != 0) return false;
    if (!S_ISDIR(file.st_mode)) { errno = ELOOP; return false; }
    if (end == std::string::npos) return true;
    begin = end + 1;
  }
  errno = EINVAL;
  return false;
}

inline bool owned_regular(const struct stat &file) {
  return S_ISREG(file.st_mode) && file.st_uid == geteuid() && file.st_nlink == 1;
}
inline bool same_file(const struct stat &a, const struct stat &b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

// Lock files remain in place after release: unlinking them would allow two
// processes to hold locks on different inodes for the same logical job.
class ExclusiveLease {
 public:
  ExclusiveLease() = default;
  ExclusiveLease(const ExclusiveLease &) = delete;
  ExclusiveLease &operator=(const ExclusiveLease &) = delete;
  ~ExclusiveLease() { release(); }

  LockResult acquire(const std::string &path) {
    if (fd_ >= 0 || path.empty() || path.front() != '/') return LockResult::Unsafe;
    const size_t slash = path.find_last_of('/');
    if (slash == 0 || !safe_directory(path.substr(0, slash)))
      return errno == ENOENT || errno == EACCES ? LockResult::Io : LockResult::Unsafe;
    struct stat before{};
    if (lstat(path.c_str(), &before) == 0) {
      if (!owned_regular(before)) return LockResult::Unsafe;
    } else if (errno != ENOENT) return LockResult::Io;
    const int opened = open(path.c_str(), O_CREAT | O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (opened < 0) return errno == ELOOP ? LockResult::Unsafe : LockResult::Io;
    struct stat actual{}, visible{};
    if (fstat(opened, &actual) != 0 || lstat(path.c_str(), &visible) != 0) {
      (void)close(opened);
      return LockResult::Io;
    }
    if (!owned_regular(actual) || !owned_regular(visible) || !same_file(actual, visible)) {
      (void)close(opened);
      return LockResult::Unsafe;
    }
    int result;
    do { result = flock(opened, LOCK_EX | LOCK_NB); } while (result != 0 && errno == EINTR);
    if (result != 0) {
      const int error = errno;
      (void)close(opened);
      return error == EWOULDBLOCK || error == EAGAIN ? LockResult::Busy : LockResult::Io;
    }
    if (lstat(path.c_str(), &visible) != 0 || !owned_regular(visible) ||
        !same_file(actual, visible)) {
      (void)flock(opened, LOCK_UN);
      (void)close(opened);
      return LockResult::Unsafe;
    }
    fd_ = opened;
    return LockResult::Acquired;
  }

  void release() {
    if (fd_ < 0) return;
    (void)flock(fd_, LOCK_UN);
    (void)close(fd_);
    fd_ = -1;
  }
 private:
  int fd_ = -1;
};

// O_NONBLOCK prevents a raced-in FIFO/device from hanging either startup or
// recovery. The exact bounded content and inode are checked through one FD.
inline MarkerResult known_marker(const std::string &path, const char *version,
                                  struct stat *identity = nullptr) {
  if (!version) return MarkerResult::Unsafe;
  const size_t size = std::strlen(version);
  if (!size || size >= 128) return MarkerResult::Unsafe;
  struct stat visible{};
  if (lstat(path.c_str(), &visible) != 0)
    return errno == ENOENT ? MarkerResult::Missing : MarkerResult::Io;
  if (!owned_regular(visible) || visible.st_size != static_cast<off_t>(size))
    return MarkerResult::Unsafe;
  const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return errno == ELOOP ? MarkerResult::Unsafe : MarkerResult::Io;
  struct stat opened{};
  MarkerResult result = MarkerResult::Known;
  if (fstat(fd, &opened) != 0) result = MarkerResult::Io;
  else if (!owned_regular(opened) || !same_file(visible, opened) ||
           opened.st_size != static_cast<off_t>(size)) result = MarkerResult::Unsafe;
  char bytes[128]{};
  size_t used = 0;
  while (result == MarkerResult::Known && used < size) {
    const ssize_t got = read(fd, bytes + used, size - used);
    if (got < 0 && errno == EINTR) continue;
    if (got < 0) { result = MarkerResult::Io; break; }
    if (!got) { result = MarkerResult::Unsafe; break; }
    used += static_cast<size_t>(got);
  }
  char extra;
  if (result == MarkerResult::Known) {
    ssize_t got;
    do { got = read(fd, &extra, 1); } while (got < 0 && errno == EINTR);
    if (got < 0) result = MarkerResult::Io;
    else if (got != 0 || std::memcmp(bytes, version, size) != 0) result = MarkerResult::Unsafe;
  }
  if (close(fd) != 0 && result == MarkerResult::Known) result = MarkerResult::Io;
  if (result == MarkerResult::Known) {
    struct stat current{};
    if (lstat(path.c_str(), &current) != 0) return MarkerResult::Io;
    if (!owned_regular(current) || !same_file(opened, current)) return MarkerResult::Unsafe;
    if (identity) *identity = opened;
  }
  return result;
}

inline bool remove_known_marker(const std::string &path, const char *version,
                                 const struct stat &identity) {
  struct stat current{};
  return known_marker(path, version, &current) == MarkerResult::Known &&
         same_file(identity, current) && unlink(path.c_str()) == 0;
}
} // namespace onion::hyper::install_detail
