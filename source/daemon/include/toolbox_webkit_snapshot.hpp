#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace onion {

struct ToolboxWebkitSnapshot {
  int shellui_pid = -1;
  int web_pid = -1;
};

/* Layout values are offsetof/sizeof from the existing PS5 kinfo_proc, not
 * newly selected firmware offsets. Kept separate so malformed snapshots and
 * the final recheck can be exercised without a console. */
struct ToolboxProcessLayout {
  size_t pid_offset;
  size_t comm_offset;
  size_t comm_bytes;
  size_t thread_offset;
  size_t thread_bytes;
};

inline bool toolbox_parse_webkit_snapshot(const void *buffer, size_t size,
    const ToolboxProcessLayout &layout, ToolboxWebkitSnapshot *out) {
  if (!buffer || !size || !out || !layout.comm_bytes || !layout.thread_bytes)
    return false;
  ToolboxWebkitSnapshot parsed;
  const auto *cursor = static_cast<const unsigned char *>(buffer);
  size_t remaining = size;
  while (remaining > 0) {
    int32_t record_size = 0;
    if (remaining < sizeof(record_size)) return false;
    std::memcpy(&record_size, cursor, sizeof(record_size));
    if (record_size <= 0 || static_cast<size_t>(record_size) > remaining)
      return false;
    const size_t length = static_cast<size_t>(record_size);
    if (layout.pid_offset > length || sizeof(int32_t) > length - layout.pid_offset ||
        layout.comm_offset > length || layout.comm_bytes > length - layout.comm_offset ||
        layout.thread_offset > length || layout.thread_bytes > length - layout.thread_offset)
      return false;
    int32_t pid = -1;
    std::memcpy(&pid, cursor + layout.pid_offset, sizeof(pid));
    const char *comm = reinterpret_cast<const char *>(cursor + layout.comm_offset);
    const char *thread = reinterpret_cast<const char *>(cursor + layout.thread_offset);
    if (!std::memchr(comm, '\0', layout.comm_bytes) ||
        !std::memchr(thread, '\0', layout.thread_bytes))
      return false;
    const bool shellui = std::strcmp(comm, "SceShellUI") == 0 ||
                         std::strcmp(thread, "SceShellUI") == 0;
    const bool web = std::strcmp(comm, "SceNKWebProcess") == 0 ||
                     std::strcmp(thread, "SceNKWebProcess") == 0;
    if (shellui && pid > 1) {
      if (parsed.shellui_pid > 1 && parsed.shellui_pid != pid) return false;
      parsed.shellui_pid = pid;
    }
    if (web && pid > 1) parsed.web_pid = pid;
    cursor += length;
    remaining -= length;
  }
  *out = parsed;
  return true;
}

template <typename SnapshotReader>
bool toolbox_pre_attach_allowed(int target_pid, SnapshotReader reader,
                                 ToolboxWebkitSnapshot *observed = nullptr) {
  ToolboxWebkitSnapshot snapshot;
  const bool valid = reader(&snapshot);
  if (observed) *observed = snapshot;
  return valid && target_pid > 1 && snapshot.shellui_pid == target_pid &&
         snapshot.web_pid <= 1;
}

} // namespace onion
