#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::hyper {
inline constexpr char kRuntimeSelectionPath[] = "/system_tmp/hyper_assistant_selection.cfg";
inline constexpr size_t kRuntimeSelectionMaxBytes = 79;
struct RuntimeSelection { int model_id = 0; int voice_id = 0; };

inline bool valid_runtime_selection(const RuntimeSelection &value) {
  return value.model_id >= 0 && value.model_id <= 7 &&
         value.voice_id >= 0 && value.voice_id <= 255;
}

inline bool parse_runtime_selection(const char *data, size_t size,
                                     RuntimeSelection *out) {
  constexpr char prefix[] = "hyper-selection 35 ";
  if (!data || !out || size > kRuntimeSelectionMaxBytes || size <= sizeof(prefix) ||
      std::memcmp(data, prefix, sizeof(prefix) - 1) != 0) return false;
  size_t cursor = sizeof(prefix) - 1;
  auto integer = [&](int *value) {
    *value = 0;
    unsigned digits = 0;
    while (cursor < size && data[cursor] >= '0' && data[cursor] <= '9') {
      if (++digits > 3) return false;
      *value = *value * 10 + data[cursor++] - '0';
    }
    return digits != 0;
  };
  RuntimeSelection value;
  if (!integer(&value.model_id) || cursor >= size || data[cursor++] != ' ' ||
      !integer(&value.voice_id) || cursor + 1 != size || data[cursor] != '\n' ||
      !valid_runtime_selection(value)) return false;
  *out = value;
  return true;
}

inline bool read_runtime_selection(RuntimeSelection *out,
                                    const char *path = kRuntimeSelectionPath) {
  if (!out || !path) return false;
  const int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat file{};
  char bytes[kRuntimeSelectionMaxBytes + 1]{};
  bool valid = fstat(fd, &file) == 0 && S_ISREG(file.st_mode) && file.st_size > 0 &&
               file.st_size <= static_cast<off_t>(kRuntimeSelectionMaxBytes);
  size_t used = 0;
  while (valid && used < static_cast<size_t>(file.st_size)) {
    const ssize_t got = read(fd, bytes + used, static_cast<size_t>(file.st_size) - used);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) { valid = false; break; }
    used += static_cast<size_t>(got);
  }
  char extra = 0;
  if (valid && read(fd, &extra, 1) != 0) valid = false;
  (void)close(fd);
  return valid && parse_runtime_selection(bytes, used, out);
}

inline bool read_selection(RuntimeSelection *out,
                            const char *path = kRuntimeSelectionPath) {
  return read_runtime_selection(out, path);
}
inline bool read_selection(RuntimeSelection &out,
                            const char *path = kRuntimeSelectionPath) {
  return read_runtime_selection(&out, path);
}

// The daemon serializes publication with its selection transaction. The
// worker only reads this file; it never writes or unlinks the shared record.
inline bool write_runtime_selection(const RuntimeSelection &value,
                                     const char *path = kRuntimeSelectionPath) {
  if (!path || !valid_runtime_selection(value)) return false;
  struct stat existing{};
  if (lstat(path, &existing) == 0) {
    if (!S_ISREG(existing.st_mode) || existing.st_uid != geteuid()) return false;
    // Generic settings reloads and ON/OFF changes need no new durable write
    // when this exact validated model/voice record is already published.
    RuntimeSelection published;
    if (read_runtime_selection(&published, path) &&
        published.model_id == value.model_id && published.voice_id == value.voice_id)
      return true;
  } else if (errno != ENOENT) return false;
  char temporary[256]{}, bytes[kRuntimeSelectionMaxBytes + 1]{};
  const int path_size = std::snprintf(temporary, sizeof(temporary), "%s.%d.tmp", path, getpid());
  const int count = std::snprintf(bytes, sizeof(bytes), "hyper-selection 35 %d %d\n",
                                  value.model_id, value.voice_id);
  if (path_size <= 0 || static_cast<size_t>(path_size) >= sizeof(temporary) ||
      count <= 0 || static_cast<size_t>(count) >= sizeof(bytes)) return false;
  const int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (fd < 0) return false;
  size_t used = 0;
  bool valid = true;
  while (used < static_cast<size_t>(count)) {
    const ssize_t put = write(fd, bytes + used, static_cast<size_t>(count) - used);
    if (put < 0 && errno == EINTR) continue;
    if (put <= 0) { valid = false; break; }
    used += static_cast<size_t>(put);
  }
  valid = valid && fsync(fd) == 0;
  if (close(fd) != 0) valid = false;
  if (valid && rename(temporary, path) == 0) return true;
  (void)unlink(temporary);
  return false;
}
} // namespace onion::hyper
