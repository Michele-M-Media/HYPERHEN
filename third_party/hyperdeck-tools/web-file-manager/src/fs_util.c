#include "filemgr_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/vfs.h>
#else
#include <sys/mount.h>
#endif
#include <time.h>
#include <unistd.h>

#include "path_util.h"
#include "smb.h"
#include "vfs.h"

int
ignore_chmod_error(int err) {
  return err == ENOTSUP || err == EPERM || err == EINVAL || err == EROFS;
}

#ifndef __linux__
static int
fs_type_has_unix_modes(const char *type) {
  return strcmp(type, "exfat") &&
         strcmp(type, "exfatfs") &&
         strcmp(type, "msdosfs") &&
         strcmp(type, "fat") &&
         strcmp(type, "vfat");
}
#endif

static int
path_has_unix_modes(const char *path) {
  if(smb_path(path)) return 0;
#ifdef __linux__
  (void)path;
  return 1;
#else
  struct statfs fs;

  if(statfs(path, &fs)) {
    return 1;
  }
  return fs_type_has_unix_modes(fs.f_fstypename);
#endif
}

static int
fd_has_unix_modes(int fd) {
  if(wfm_fd_remote(fd)) return 0;
#ifdef __linux__
  (void)fd;
  return 1;
#else
  struct statfs fs;

  if(fstatfs(fd, &fs)) {
    return 1;
  }
  return fs_type_has_unix_modes(fs.f_fstypename);
#endif
}

int
chmod_path_mode(const char *path, unsigned int mode) {
  if(smb_path(path)) { errno = ENOTSUP; return -1; }
  if(!path_has_unix_modes(path)) {
    return 0;
  }
  if(chmod(path, (mode_t)(mode & 0777))) {
    /* A filesystem that does not implement Unix modes is compatible with the
       paste behavior. Real permission and read-only errors must reach the UI. */
    if(errno != ENOTSUP && errno != EINVAL) {
      return -1;
    }
  }
  return 0;
}

int
chmod_path_0777(const char *path) {
  if(!path_has_unix_modes(path)) {
    return 0;
  }
  if(chmod(path, 0777) && !ignore_chmod_error(errno)) {
    return -1;
  }
  return 0;
}

int
fchmod_0777(int fd) {
  if(!fd_has_unix_modes(fd)) {
    return 0;
  }
  if(fchmod(fd, 0777) && !ignore_chmod_error(errno)) {
    return -1;
  }
  return 0;
}

static int
target_statvfs(const char *target, struct statvfs *vfs) {
  char parent[PATH_MAX];

  if(!statvfs(target, vfs)) {
    return 0;
  }
  if(path_dirname(target, parent, sizeof(parent))) {
    return -1;
  }
  return statvfs(parent, vfs);
}

int
target_available_space(const char *target, unsigned long long *available) {
  struct statvfs vfs;
  unsigned long long block_size;

  if(target_statvfs(target, &vfs)) {
    return -1;
  }
  block_size = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
  *available = (unsigned long long)vfs.f_bavail * block_size;
  return 0;
}

int
check_target_space(const char *target, unsigned long long required,
                   char *error, size_t error_size,
                   char *code, size_t code_size,
                   char *arg, size_t arg_size) {
  unsigned long long available;

  if(!required) {
    return 0;
  }
  if(target_available_space(target, &available)) {
    snprintf(error, error_size, "cannot read target free space");
    snprintf(code, code_size, "space_check_failed");
    snprintf(arg, arg_size, "%s", target);
    return -1;
  }
  if(available < required) {
    snprintf(error, error_size,
             "not enough target space, required %llu bytes, available %llu bytes",
             required, available);
    snprintf(code, code_size, "no_space");
    snprintf(arg, arg_size, "%llu,%llu", required, available);
    errno = ENOSPC;
    return -1;
  }
  return 0;
}

static void
set_error_detail(char *error, size_t error_size, char *code, size_t code_size,
                 char *arg, size_t arg_size, const char *error_code,
                 const char *message, const char *path) {
  snprintf(error, error_size, "%s: %s", message, path);
  snprintf(code, code_size, "%s", error_code);
  snprintf(arg, arg_size, "%s", path);
}

static void
set_permission_error_detail(char *error, size_t error_size,
                            char *code, size_t code_size,
                            char *arg, size_t arg_size,
                            const char *error_code, const char *message,
                            const char *path) {
  struct stat st;

  set_error_detail(error, error_size, code, code_size, arg, arg_size,
                   error_code, message, path);
  if(!stat(path, &st)) {
    snprintf(arg, arg_size, "%s (mode=%04o, uid=%lu, gid=%lu)", path,
             (unsigned int)(st.st_mode & 07777),
             (unsigned long)st.st_uid, (unsigned long)st.st_gid);
  }
}

static int
mode_access_stat(const struct stat *st, int mode) {
  mode_t allowed;
  uid_t uid = geteuid();

  if(uid == 0) {
    if(!(mode & X_OK) || !S_ISREG(st->st_mode) ||
       (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
      return 0;
    }
  } else if(uid == st->st_uid) {
    allowed = (st->st_mode >> 6) & 7;
    if((allowed & mode) == (mode_t)mode) {
      return 0;
    }
  } else {
    gid_t gid = getegid();
    int group_match = gid == st->st_gid;

    if(!group_match) {
      int count = getgroups(0, NULL);
      gid_t *groups = count > 0 ? malloc((size_t)count * sizeof(*groups)) : NULL;

      if(groups && getgroups(count, groups) == count) {
        int i;
        for(i = 0; i < count; i++) {
          if(groups[i] == st->st_gid) {
            group_match = 1;
            break;
          }
        }
      }
      free(groups);
    }
    allowed = group_match ? (st->st_mode >> 3) & 7 : st->st_mode & 7;
    if((allowed & mode) == (mode_t)mode) {
      return 0;
    }
  }
  errno = EACCES;
  return -1;
}

int
mode_access(const char *path, int mode) {
  struct stat st;

  return stat(path, &st) ? -1 : mode_access_stat(&st, mode);
}

static int
probe_dir_writable(const char *path) {
  char name[80];
  char probe[PATH_MAX];
  int attempt;

  for(attempt = 0; attempt < 16; attempt++) {
    int fd;
    int error = 0;

    snprintf(name, sizeof(name), ".web-file-mgr-%ld-%lld-%d.tmp",
             (long)getpid(), (long long)time(NULL), attempt);
    if(path_join(probe, sizeof(probe), path, name)) {
      return -1;
    }
    fd = open(probe, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if(fd < 0) {
      if(errno == EEXIST) {
        continue;
      }
      return -1;
    }
    if(close(fd)) {
      error = errno;
    }
    if(unlink(probe) && !error) {
      error = errno;
    }
    if(error) {
      errno = error;
      return -1;
    }
    return 0;
  }
  errno = EEXIST;
  return -1;
}

static int
probe_file_writable(const char *path) {
  int fd = open(path, O_WRONLY);

  return fd < 0 ? -1 : close(fd);
}

static int
path_seen(char **paths, size_t count, const char *path) {
  size_t i;

  for(i = 0; i < count; i++) {
    if(!strcmp(paths[i], path)) {
      return 1;
    }
  }
  return 0;
}

static int
remember_path(char ***paths, size_t *count, const char *path) {
  char **tmp;

  if(path_seen(*paths, *count, path)) {
    return 0;
  }
  tmp = realloc(*paths, sizeof(char *) * (*count + 1));
  if(!tmp) {
    errno = ENOMEM;
    return -1;
  }
  *paths = tmp;
  (*paths)[*count] = strdup(path);
  if(!(*paths)[*count]) {
    errno = ENOMEM;
    return -1;
  }
  (*count)++;
  return 0;
}

static int
probe_dir_once(const char *path, char ***checked, size_t *checked_count) {
  if(path_seen(*checked, *checked_count, path)) {
    return 0;
  }
  if(probe_dir_writable(path)) {
    return -1;
  }
  return remember_path(checked, checked_count, path);
}

int
check_target_writable(const char *target, char ***checked_dirs,
                      size_t *checked_dir_count, char *error, size_t error_size,
                      char *code, size_t code_size, char *arg, size_t arg_size) {
  struct stat st;
  char parent[PATH_MAX];

  if(!stat(target, &st)) {
    if(S_ISDIR(st.st_mode)) {
      if(probe_dir_once(target, checked_dirs, checked_dir_count)) {
        set_permission_error_detail(error, error_size, code, code_size,
                                    arg, arg_size, "target_dir_not_writable",
                                    "target directory is not writable", target);
        return -1;
      }
    } else {
      if(probe_file_writable(target)) {
        set_permission_error_detail(error, error_size, code, code_size,
                                    arg, arg_size, "target_file_not_writable",
                                    "target file is not writable", target);
        return -1;
      }
      goto check_parent;
    }
    return 0;
  }

  if(errno != ENOENT) {
    set_error_detail(error, error_size, code, code_size, arg, arg_size,
                     "target_check_failed", "cannot check target path", target);
    return -1;
  }

check_parent:
  if(path_dirname(target, parent, sizeof(parent))) {
    set_error_detail(error, error_size, code, code_size, arg, arg_size,
                     "target_check_failed", "cannot check target path", target);
    return -1;
  }
  if(probe_dir_once(parent, checked_dirs, checked_dir_count)) {
    set_permission_error_detail(error, error_size, code, code_size,
                                arg, arg_size, "target_parent_not_writable",
                                "current directory is not writable", parent);
    return -1;
  }
  return 0;
}
