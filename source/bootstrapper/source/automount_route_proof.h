/* Atomic, short-lived handoff to the integrated Automount beta9 process. */
#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum HyperhenAutomountEngine {
  HYPERHEN_AUTOMOUNT_ENGINE_CORE,
  HYPERHEN_AUTOMOUNT_ENGINE_LEGACY,
  HYPERHEN_AUTOMOUNT_ENGINE_BYPERVISOR,
};

struct HyperhenAutomountRouteProof {
  bool published;
  dev_t device;
  ino_t inode;
  char path[256];
};

static inline bool hyperhen_automount_route_publish(
    const char *directory, uint32_t firmware,
    enum HyperhenAutomountEngine engine, bool lite, pid_t launcher,
    struct HyperhenAutomountRouteProof *proof) {
  const char *engine_name = NULL;
  if (!directory || !proof || proof->published || launcher <= 1) {
    errno = EINVAL;
    return false;
  }
  switch (engine) {
  case HYPERHEN_AUTOMOUNT_ENGINE_CORE: engine_name = "core"; break;
  case HYPERHEN_AUTOMOUNT_ENGINE_LEGACY: engine_name = "legacy"; break;
  case HYPERHEN_AUTOMOUNT_ENGINE_BYPERVISOR: engine_name = "bypervisor"; break;
  default: errno = EINVAL; return false;
  }
  if (lite && engine != HYPERHEN_AUTOMOUNT_ENGINE_CORE) {
    errno = EINVAL;
    return false;
  }
  /* The existing engine readiness protocol must already have created this
   * directory; never create a substitute proof before engine readiness. */
  struct stat directory_state;
  if (lstat(directory, &directory_state) != 0) return false;
  if (!S_ISDIR(directory_state.st_mode)) {
    errno = ENOTDIR;
    return false;
  }
  int length = snprintf(proof->path, sizeof(proof->path),
                        "%s/automount_route", directory);
  if (length <= 0 || (size_t)length >= sizeof(proof->path)) {
    errno = ENAMETOOLONG;
    return false;
  }
  char contents[96];
  length = snprintf(contents, sizeof(contents), "v1 %08x %s %u %ld\n",
                    (unsigned)(firmware & UINT32_C(0xffff0000)), engine_name,
                    lite ? 1u : 0u, (long)launcher);
  if (length <= 0 || (size_t)length >= sizeof(contents)) {
    errno = EOVERFLOW;
    return false;
  }
  char temporary[256];
  int fd = -1;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const int amount = snprintf(temporary, sizeof(temporary),
        "%s/.automount_route.%ld.%u.tmp", directory, (long)launcher, attempt);
    if (amount <= 0 || (size_t)amount >= sizeof(temporary)) {
      errno = ENAMETOOLONG;
      return false;
    }
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
    if (fd >= 0) break;
    if (errno != EEXIST) return false;
  }
  if (fd < 0) return false;
  int saved_errno = 0;
  size_t written = 0;
  while (written < (size_t)length) {
    const ssize_t amount = write(fd, contents + written, (size_t)length - written);
    if (amount < 0 && errno == EINTR) continue;
    if (amount <= 0) {
      saved_errno = amount < 0 ? errno : EIO;
      break;
    }
    written += (size_t)amount;
  }
  struct stat owned_file;
  if (!saved_errno && fstat(fd, &owned_file) != 0) saved_errno = errno;
  if (close(fd) != 0 && !saved_errno) saved_errno = errno;
  /* rename replaces a stale final entry atomically without following it.
   * Only temporary files created by this invocation are ever unlinked. */
  if (!saved_errno && rename(temporary, proof->path) != 0) saved_errno = errno;
  if (saved_errno) {
    (void)unlink(temporary);
    errno = saved_errno;
    return false;
  }
  proof->device = owned_file.st_dev;
  proof->inode = owned_file.st_ino;
  proof->published = true;
  return true;
}

static inline bool hyperhen_automount_route_clear(
    struct HyperhenAutomountRouteProof *proof) {
  if (!proof || !proof->published) return true;
  struct stat current;
  if (lstat(proof->path, &current) != 0) {
    if (errno != ENOENT) return false;
    proof->published = false;
    return true;
  }
  /* Do not remove a replacement proof or an unrelated symlink/file. */
  if (!S_ISREG(current.st_mode) || current.st_dev != proof->device ||
      current.st_ino != proof->inode) {
    errno = ESTALE;
    return false;
  }
  if (unlink(proof->path) != 0 && errno != ENOENT) return false;
  proof->published = false;
  return true;
}
