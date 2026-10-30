#pragma once

#include <cstddef>
#include <sys/stat.h>
#include <sys/types.h>

namespace onion::daemon::hyper::detail {
struct WorkerLaunchOps {
  int (*open_file)(const char *, int, mode_t);
  ssize_t (*write_file)(int, const void *, size_t);
  int (*close_file)(int);
  int (*stat_fd)(int, struct stat *);
  int (*stat_path)(const char *, struct stat *);
  int (*unlink_file)(const char *);
  pid_t (*launch_file)(const char *, const char *);
};
#if defined(HYPER_WORKER_LAUNCH_HOST_TEST)
// Test builds replace platform boundaries only; production has no setter.
const WorkerLaunchOps &worker_launch_test_ops();
#endif
}
