#include "hyper_worker_launch.hpp"
#include "hyper_worker_launch_ops.hpp"

#include <elfldr_remote.h>
#include <onion/log.h>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace onion::daemon::hyper {
namespace {
using detail::WorkerLaunchOps;

const WorkerLaunchOps &operations() {
#if defined(HYPER_WORKER_LAUNCH_HOST_TEST)
  return detail::worker_launch_test_ops();
#else
  static const WorkerLaunchOps ops{
      [](const char *path, int flags, mode_t mode) { return open(path, flags, mode); },
      write, close, fstat, lstat, unlink,
      elfldr_remote_onion_launch_file_get_pid};
  return ops;
#endif
}

enum class StageResult { Ready, RetryBeforeContact, Failed };

struct OwnedStage {
  const WorkerLaunchOps &ops;
  const char *path;
  const char *slot;
  dev_t device = 0;
  ino_t inode = 0;
  bool identified = false;

  void cleanup() const {
    if (!identified) return;
    struct stat current{};
    if (ops.stat_path(path, &current) != 0) {
      if (errno != ENOENT)
        LOG_ERROR("Hyper R39 launch phase=cleanup_stat slot=%s errno=%d", slot, errno);
      return;
    }
    if (!S_ISREG(current.st_mode) || current.st_dev != device || current.st_ino != inode) {
      LOG_ERROR("Hyper R39 launch phase=cleanup_replaced slot=%s", slot);
      return;
    }
    if (ops.unlink_file(path) != 0 && errno != ENOENT)
      LOG_ERROR("Hyper R39 launch phase=cleanup_unlink slot=%s errno=%d", slot, errno);
  }
};

StageResult stage_file(OwnedStage &stage, const uint8_t *elf, size_t size) {
  const int fd = stage.ops.open_file(stage.path,
      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) {
    LOG_ERROR("Hyper R39 launch phase=open slot=%s errno=%d bytes=0", stage.slot, errno);
    return StageResult::RetryBeforeContact;
  }
  struct stat created{};
  if (stage.ops.stat_fd(fd, &created) != 0 || !S_ISREG(created.st_mode)) {
    const int saved = errno ? errno : EIO;
    (void)stage.ops.close_file(fd);
    // No inode proof: leave this entry rather than unlink an unknown pathname.
    LOG_ERROR("Hyper R39 launch phase=stage_identity slot=%s errno=%d bytes=0", stage.slot, saved);
    errno = saved;
    return StageResult::Failed;
  }
  stage.device = created.st_dev;
  stage.inode = created.st_ino;
  stage.identified = true;
  size_t written = 0;
  int failure = 0;
  while (written < size) {
    const ssize_t amount = stage.ops.write_file(fd, elf + written, size - written);
    if (amount < 0) {
      if (errno == EINTR) continue;
      failure = errno ? errno : EIO;
      break;
    }
    if (amount == 0 || static_cast<size_t>(amount) > size - written) {
      failure = EIO;
      break;
    }
    written += static_cast<size_t>(amount);
  }
  // Never retry close(): an EINTR result may already have released its fd.
  const int close_result = stage.ops.close_file(fd);
  const int close_error = close_result != 0 ? (errno ? errno : EIO) : 0;
  if (failure || close_error) {
    LOG_ERROR("Hyper R39 launch phase=%s slot=%s errno=%d bytes=%zu",
              failure ? "write" : "close", stage.slot,
              failure ? failure : close_error, written);
    stage.cleanup();
    stage.identified = false;
    errno = failure ? failure : close_error;
    return StageResult::RetryBeforeContact;
  }
  LOG_INFO("Hyper R39 launch phase=staged slot=%s bytes=%zu", stage.slot, written);
  return StageResult::Ready;
}
} // namespace

pid_t launch_worker(const uint8_t *elf, size_t size, const char *args, uint64_t token) {
  const pid_t owner = getpid();
  if (!elf || size < 4 || !args || !token || owner <= 1) {
    LOG_ERROR("Hyper R39 launch phase=arguments errno=%d bytes=%zu", EINVAL, size);
    errno = EINVAL;
    return -1;
  }
  char paths[2][160]{};
  const int primary = std::snprintf(paths[0], sizeof(paths[0]),
      "/system_tmp/hyper_assistant_r39.%ld.%llu.elf",
      static_cast<long>(owner), static_cast<unsigned long long>(token));
  const int fallback = std::snprintf(paths[1], sizeof(paths[1]),
      "/data/Hyper/.worker-stage-r39.%ld.%llu.elf",
      static_cast<long>(owner), static_cast<unsigned long long>(token));
  if (primary <= 0 || static_cast<size_t>(primary) >= sizeof(paths[0]) ||
      fallback <= 0 || static_cast<size_t>(fallback) >= sizeof(paths[1])) {
    LOG_ERROR("Hyper R39 launch phase=path errno=%d bytes=%zu", ENAMETOOLONG, size);
    errno = ENAMETOOLONG;
    return -1;
  }
  const auto &ops = operations();
  const char *slots[] = {"system_tmp", "data_Hyper"};
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    OwnedStage staged{ops, paths[attempt], slots[attempt]};
    const StageResult result = stage_file(staged, elf, size);
    if (result == StageResult::Failed) return -1;
    if (result == StageResult::RetryBeforeContact) continue;
    LOG_INFO("Hyper R39 launch phase=request slot=%s bytes=%zu", staged.slot, size);
    errno = 0;
    const pid_t pid = ops.launch_file(staged.path, args);
    const int response_error = errno;
    if (pid > 1)
      LOG_INFO("Hyper R39 launch phase=response slot=%s pid=%d bytes=%zu", staged.slot, static_cast<int>(pid), size);
    else
      LOG_ERROR("Hyper R39 launch phase=response_unknown slot=%s pid=%d errno=%d bytes=%zu",
                staged.slot, static_cast<int>(pid), response_error, size);
    staged.cleanup();
    // A request may have launched the child even if its response was lost.
    // Never contact the loader again after this point, on either pathname.
    if (pid <= 1) {
      errno = response_error ? response_error : EPROTO;
      return -1;
    }
    return pid;
  }
  return -1;
}
} // namespace onion::daemon::hyper
