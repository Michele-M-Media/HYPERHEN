/* Read-only ShadowMountPlus conflict check shared with the host tests. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

enum HyperhenMountPreflightResult {
  HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE = -1,
  HYPERHEN_MOUNT_PREFLIGHT_CLEAR = 0,
  HYPERHEN_MOUNT_PREFLIGHT_CONFLICT = 1,
};

/* These record fields are the same sysctl ABI used by beta9's own singleton
 * lookup (main.c), not firmware-specific kernel or ShellCore patch offsets. */
#define HYPERHEN_SMP_KINFO_PID_OFFSET 72u
#define HYPERHEN_SMP_KINFO_NAME_OFFSET 447u
#define HYPERHEN_SMP_MAX_PROCESSES 8192u
#define HYPERHEN_SMP_MAX_SNAPSHOT_BYTES (32u * 1024u * 1024u)

struct HyperhenMountPreflightOps {
  int (*snapshot)(void *opaque, bool by_pid, pid_t pid, void **records,
                  size_t *bytes);
  void (*release)(void *opaque, void *records);
  void *opaque;
};

static inline int hyperhen_mount_scan_snapshot(const void *records, size_t bytes,
                                               pid_t expected_pid,
                                               pid_t *matched_pid) {
  static const char external_name[] = "shadowmountplus.elf";
  /* The shared proc helper also recognizes the 16-character thread name. */
  static const char external_thread_name[] = "shadowmountplus.";
  const unsigned char *cursor = records;
  size_t count = 0;
  if (matched_pid) *matched_pid = 0;
  if (bytes > HYPERHEN_SMP_MAX_SNAPSHOT_BYTES || (!records && bytes))
    return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
  while (bytes) {
    int32_t record_bytes = 0;
    pid_t pid = 0;
    if (++count > HYPERHEN_SMP_MAX_PROCESSES || bytes < sizeof(record_bytes))
      return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
    memcpy(&record_bytes, cursor, sizeof(record_bytes));
    if (record_bytes <= (int32_t)HYPERHEN_SMP_KINFO_NAME_OFFSET ||
        (size_t)record_bytes > bytes ||
        HYPERHEN_SMP_KINFO_PID_OFFSET + sizeof(pid) > (size_t)record_bytes)
      return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
    memcpy(&pid, cursor + HYPERHEN_SMP_KINFO_PID_OFFSET, sizeof(pid));
    const unsigned char *name = cursor + HYPERHEN_SMP_KINFO_NAME_OFFSET;
    const size_t name_bytes = (size_t)record_bytes - HYPERHEN_SMP_KINFO_NAME_OFFSET;
    const bool full_match = name_bytes >= sizeof(external_name) &&
        memcmp(name, external_name, sizeof(external_name)) == 0;
    const bool thread_match = name_bytes >= sizeof(external_thread_name) &&
        memcmp(name, external_thread_name, sizeof(external_thread_name)) == 0;
    if (pid > 1 && (!expected_pid || pid == expected_pid) &&
        (full_match || thread_match)) {
      if (matched_pid) *matched_pid = pid;
      return HYPERHEN_MOUNT_PREFLIGHT_CONFLICT;
    }
    cursor += record_bytes;
    bytes -= (size_t)record_bytes;
  }
  return HYPERHEN_MOUNT_PREFLIGHT_CLEAR;
}

static inline int hyperhen_mount_preflight(
    const struct HyperhenMountPreflightOps *ops, pid_t *conflicting_pid) {
  if (conflicting_pid) *conflicting_pid = 0;
  if (!ops || !ops->snapshot || !ops->release)
    return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
  /* Re-read a disappearing/renamed candidate instead of treating a stale PID
   * as an active owner or silently missing another live instance. */
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    void *records = NULL;
    size_t bytes = 0;
    pid_t candidate = 0;
    if (ops->snapshot(ops->opaque, false, 0, &records, &bytes) != 0) {
      if (records) ops->release(ops->opaque, records);
      return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
    }
    int result = hyperhen_mount_scan_snapshot(records, bytes, 0, &candidate);
    if (records) ops->release(ops->opaque, records);
    if (result != HYPERHEN_MOUNT_PREFLIGHT_CONFLICT) return result;
    records = NULL;
    bytes = 0;
    if (ops->snapshot(ops->opaque, true, candidate, &records, &bytes) != 0) {
      if (records) ops->release(ops->opaque, records);
      return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
    }
    pid_t confirmed = 0;
    result = hyperhen_mount_scan_snapshot(records, bytes, candidate, &confirmed);
    if (records) ops->release(ops->opaque, records);
    if (result == HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE) return result;
    if (result == HYPERHEN_MOUNT_PREFLIGHT_CONFLICT) {
      if (conflicting_pid) *conflicting_pid = confirmed;
      return result;
    }
  }
  return HYPERHEN_MOUNT_PREFLIGHT_UNAVAILABLE;
}
