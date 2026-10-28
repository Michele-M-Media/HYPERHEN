/* SMB filesystem adapter derived from owendswang/ps5-web-file-manager
 * (src/vfs.c), GPL-3.0-or-later. See licenses/SMB-NOTICES.txt.
 * ShadowMountPlus integration and named-connection support. */
#include "sm_platform.h"
#define SM_VFS_IMPLEMENTATION
#include "sm_runtime.h"
#include "sm_smb.h"
#include "sm_time.h"
#include "sm_log.h"
#include "sm_vfs.h"
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define SMB_FD_BASE 0x40000000
#define SMB_FD_COUNT 64
#define SMB_IO_REQUESTS 8
#define SMB_IO_BLOCK (1024 * 1024)
#define SMB_IO_WRITE_BLOCK (4 * 1024 * 1024)
enum { SMB_FLUSH_NONE, SMB_FLUSH_PENDING, SMB_FLUSH_DONE };
typedef struct remote_file {
  struct smb2_context *ctx;
  sm_vfs_dir_t *owner;
  struct smb2fh *file;
  dev_t device;
  int used;
  int flush_state, flush_error;
} remote_file_t;
static remote_file_t remote_files[SMB_FD_COUNT];
struct sm_vfs_dir {
  DIR *native;
  struct smb2_context *ctx;
  struct smb2dir *remote;
  struct dirent entry;
  struct smb2dirent *last;
  int error;
  dev_t device;
  sm_smb_address_t address;
  unsigned users;
  bool closed;
  pthread_t thread;
  struct sm_vfs_dir *next;
};
static sm_vfs_dir_t *remote_dirs;

static struct smb2_context *file_context(const remote_file_t *file) {
  return file->owner ? file->owner->ctx : file->ctx;
}

/* A directory pins its authenticated context while child files borrow it.
 * Outstanding files may outlive closedir; the last borrower releases the owner. */
static void release_directory(sm_vfs_dir_t *dir) {
  if (!dir->closed || dir->users) return;
  if (dir->ctx) smb2_destroy_context(dir->ctx);
  memset(&dir->address, 0, sizeof(dir->address));
  free(dir);
}

static void invalidate_directory(sm_vfs_dir_t *dir, int error) {
  struct smb2_context *ctx = dir->ctx;
  dir->ctx = NULL;
  dir->remote = NULL;
  dir->last = NULL;
  dir->error = error;
  if (ctx) smb2_destroy_context(ctx);
}

static bool directory_matches_address(const sm_vfs_dir_t *dir, const sm_smb_address_t *address) {
  const sm_smb_address_t *saved = &dir->address;
  return dir->ctx && pthread_equal(dir->thread, pthread_self()) &&
         !strcasecmp(saved->server, address->server) &&
         !strcasecmp(saved->share, address->share) && !strcmp(saved->user, address->user) &&
         !strcmp(saved->domain, address->domain) && !strcmp(saved->password, address->password);
}

static sm_vfs_dir_t *find_directory_context(const sm_smb_address_t *address) {
  for (sm_vfs_dir_t *dir = remote_dirs; dir; dir = dir->next)
    if (directory_matches_address(dir, address)) return dir;
  return NULL;
}

static void clear_directory_stat_cache(dev_t device) {
  for (sm_vfs_dir_t *dir = remote_dirs; dir; dir = dir->next)
    if (dir->device == device) dir->last = NULL;
}

int sm_vfs_remote_fd(int fd) {
  return fd >= SMB_FD_BASE;
}
static remote_file_t *remote_file(int fd) {
  if (fd < SMB_FD_BASE || fd - SMB_FD_BASE >= SMB_FD_COUNT ||
      !remote_files[fd - SMB_FD_BASE].used) {
    errno = EBADF;
    return NULL;
  }
  if (!file_context(&remote_files[fd - SMB_FD_BASE])) {
    errno = EIO;
    return NULL;
  }
  return &remote_files[fd - SMB_FD_BASE];
}

static void file_flush_complete(struct smb2_context *ctx, int status, void *data, void *opaque) {
  (void)ctx;
  (void)data;
  remote_file_t *file = opaque;
  file->flush_error = status < 0 ? -status : status ? EIO : 0;
  file->flush_state = SMB_FLUSH_DONE;
}

static int start_file_flush(remote_file_t *file) {
  if (file->flush_state != SMB_FLUSH_NONE) {
    if (!file->flush_error) return 0;
    errno = file->flush_error;
    return -1;
  }
  file->flush_state = SMB_FLUSH_PENDING;
  int result = smb2_fsync_async(file_context(file), file->file, file_flush_complete, file);
  if (result < 0) {
    file->flush_state = SMB_FLUSH_DONE;
    file->flush_error = -result;
  }
  return sm_smb_result(result);
}

/* Caller holds the client lock. The registry slot remains live for callbacks. */
static int wait_file_flush(remote_file_t *file) {
  uint64_t deadline = monotonic_time_us() + 5000000;
  while (file->flush_state == SMB_FLUSH_PENDING) {
    struct smb2_context *ctx = file_context(file);
    if (!ctx) {
      file->flush_error = EIO;
      file->flush_state = SMB_FLUSH_DONE;
      break;
    }
    struct pollfd pfd = {.fd = smb2_get_fd(ctx), .events = smb2_which_events(ctx)};
    int result = poll(&pfd, 1, 1000);
    if (result < 0 && errno == EINTR) continue;
    bool stopping = should_stop_requested() || runtime_sleep_mode_active();
    int error = result < 0 ? errno : stopping ? ECANCELED :
                smb2_service(ctx, pfd.revents) < 0 ? EIO : 0;
    if (!error && file->flush_state == SMB_FLUSH_PENDING && monotonic_time_us() >= deadline)
      error = ETIMEDOUT;
    if (error) {
      if (file->owner)
        invalidate_directory(file->owner, error);
      else {
        smb2_destroy_context(ctx);
        file->ctx = NULL;
        file->file = NULL;
      }
      file->flush_error = error;
      file->flush_state = SMB_FLUSH_DONE;
    }
  }
  if (file->flush_error) {
    errno = file->flush_error;
    return -1;
  }
  return 0;
}

static const char *path_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}
static int path_dirname(const char *path, char *parent, size_t size) {
  const char *slash = strrchr(path, '/');
  if (!slash || (size_t)(slash - path) >= size) return -1;
  memcpy(parent, path, (size_t)(slash - path));
  parent[slash - path] = 0;
  return 0;
}
static uint32_t path_hash(const char *path, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; i++)
    hash = (hash ^ (unsigned char)tolower((unsigned char)path[i])) * 16777619u;
  return hash;
}
static dev_t remote_device(const sm_smb_address_t *a) {
  return (dev_t)(0x80000000u |
                 (path_hash(a->server, strlen(a->server)) ^ path_hash(a->share, strlen(a->share))));
}
static void convert_stat(const struct smb2_stat_64 *in, dev_t device, struct stat *out) {
  memset(out, 0, sizeof(*out));
  out->st_dev = device;
  out->st_ino = in->smb2_ino;
  out->st_nlink = in->smb2_nlink ? in->smb2_nlink : 1;
  out->st_mode = (in->smb2_type == SMB2_TYPE_DIRECTORY ? S_IFDIR
                  : in->smb2_type == SMB2_TYPE_LINK    ? S_IFLNK
                                                       : S_IFREG) |
                 0777;
  out->st_size = (off_t)in->smb2_size;
  out->st_atim.tv_sec = (time_t)in->smb2_atime;
  out->st_atim.tv_nsec = (long)in->smb2_atime_nsec;
  out->st_mtim.tv_sec = (time_t)in->smb2_mtime;
  out->st_mtim.tv_nsec = (long)in->smb2_mtime_nsec;
  out->st_ctim.tv_sec = (time_t)in->smb2_ctime;
  out->st_ctim.tv_nsec = (long)in->smb2_ctime_nsec;
}

int sm_vfs_stat(const char *path, struct stat *st) {
  if (!sm_smb_path(path)) return stat(path, st);
  sm_smb_lock();
  sm_smb_address_t a;
  if (sm_smb_parse_address(path, &a)) { sm_smb_unlock(); return -1; }
  char parent[PATH_MAX];
  if (!path_dirname(path, parent, sizeof(parent))) {
    for (sm_vfs_dir_t *dir = remote_dirs; dir; dir = dir->next) {
      if (directory_matches_address(dir, &a) && !strcmp(dir->address.path, parent) && dir->last &&
          !strcmp(dir->last->name, path_basename(path))) {
        convert_stat(&dir->last->st, dir->device, st);
        sm_smb_unlock();
        return 0;
      }
    }
  }
  sm_vfs_dir_t *owner = find_directory_context(&a);
  struct smb2_context *ctx = owner ? owner->ctx : sm_smb_connect_path(path, &a);
  struct smb2_stat_64 remote;
  int result = ctx ? sm_smb_result(smb2_stat(ctx, a.relative, &remote)) : -1;
  int error = errno;
  if (!result) convert_stat(&remote, remote_device(&a), st);
  if (owner) {
    if (result < 0 && (error == EIO || error == ETIMEDOUT || error == ECONNRESET))
      invalidate_directory(owner, error);
  } else if (ctx)
    smb2_destroy_context(ctx);
  sm_smb_unlock();
  errno = error;
  return result;
}
int sm_vfs_lstat(const char *path, struct stat *st) {
  return sm_smb_path(path) ? sm_vfs_stat(path, st) : lstat(path, st);
}
int sm_vfs_fstat(int fd, struct stat *st) {
  if (!sm_vfs_remote_fd(fd)) return fstat(fd, st);
  sm_smb_lock();
  remote_file_t *f = remote_file(fd);
  struct smb2_stat_64 remote;
  int result = f ? sm_smb_result(smb2_fstat(file_context(f), f->file, &remote)) : -1;
  if (!result) {
    convert_stat(&remote, f->device, st);
  }
  sm_smb_unlock();
  return result;
}
int sm_vfs_statvfs(const char *path, struct statvfs *st) {
  if (!sm_smb_path(path)) return statvfs(path, st);
  sm_smb_lock();
  sm_smb_address_t a;
  struct smb2_context *ctx = sm_smb_connect_path(path, &a);
  struct smb2_statvfs remote;
  int result = ctx ? sm_smb_result(smb2_statvfs(ctx, a.relative, &remote)) : -1;
  int error = errno;
  if (!result) {
    memset(st, 0, sizeof(*st));
    st->f_bsize = remote.f_bsize;
    st->f_frsize = remote.f_frsize;
    st->f_blocks = remote.f_blocks;
    st->f_bfree = remote.f_bfree;
    st->f_bavail = remote.f_bavail;
    st->f_files = remote.f_files;
    st->f_ffree = remote.f_ffree;
    st->f_favail = remote.f_favail;
    st->f_flag = remote.f_flag;
    st->f_namemax = remote.f_namemax;
  }
  if (ctx) smb2_destroy_context(ctx);
  sm_smb_unlock();
  errno = error;
  return result;
}

sm_vfs_dir_t *sm_vfs_opendir(const char *path) {
  sm_vfs_dir_t *dir = calloc(1, sizeof(*dir));
  if (!dir) return NULL;
  if (!sm_smb_path(path)) {
    if ((dir->native = opendir(path))) return dir;
    free(dir);
    return NULL;
  }
  sm_smb_lock();
  sm_smb_address_t a;
  dir->ctx = sm_smb_connect_path(path, &a);
  if (dir->ctx) dir->remote = smb2_opendir(dir->ctx, a.relative);
  if (!dir->remote) {
    int error = dir->ctx ? nterror_to_errno(smb2_get_nterror(dir->ctx)) : errno;
    if (!error) error = EIO;
    if (dir->ctx) smb2_destroy_context(dir->ctx);
    free(dir);
    sm_smb_unlock();
    errno = error;
    return NULL;
  }
  dir->address = a;
  dir->thread = pthread_self();
  dir->device = remote_device(&a);
  dir->next = remote_dirs;
  remote_dirs = dir;
  sm_smb_unlock();
  return dir;
}
struct dirent *sm_vfs_readdir(sm_vfs_dir_t *dir) {
  if (dir->native) {
    if (dir->error) {
      errno = dir->error;
      return NULL;
    }
    errno = 0;
    struct dirent *entry = readdir(dir->native);
    if (!entry) dir->error = errno;
    return entry;
  }
  sm_smb_lock();
  if (dir->error) {
    int error = dir->error;
    sm_smb_unlock();
    errno = error;
    return NULL;
  }
  dir->last = smb2_readdir(dir->ctx, dir->remote);
  if (!dir->last) {
    sm_smb_unlock();
    return NULL;
  }
  if (strlen(dir->last->name) >= sizeof(dir->entry.d_name)) {
    dir->last = NULL;
    dir->error = ENAMETOOLONG;
    sm_smb_unlock();
    errno = dir->error;
    return NULL;
  }
  memset(&dir->entry, 0, sizeof(dir->entry));
  strcpy(dir->entry.d_name, dir->last->name);
  dir->entry.d_ino = dir->last->st.smb2_ino;
  dir->entry.d_type = dir->last->st.smb2_type == SMB2_TYPE_DIRECTORY ? DT_DIR : DT_REG;
  sm_smb_unlock();
  return &dir->entry;
}
int sm_vfs_dir_error(sm_vfs_dir_t *dir) {
  if (!dir->native) sm_smb_lock();
  int error = dir->error;
  if (!dir->native) sm_smb_unlock();
  if (error) errno = error;
  return error;
}
int sm_vfs_closedir(sm_vfs_dir_t *dir) {
  int saved_errno = errno;
  int result = 0;
  if (dir->native)
    result = closedir(dir->native);
  else {
    sm_smb_lock();
    sm_vfs_dir_t **p = &remote_dirs;
    while (*p && *p != dir)
      p = &(*p)->next;
    if (*p) *p = dir->next;
    if (dir->ctx && dir->remote) smb2_closedir(dir->ctx, dir->remote);
    dir->remote = NULL;
    dir->last = NULL;
    dir->closed = true;
    release_directory(dir);
    sm_smb_unlock();
    errno = saved_errno;
    return 0;
  }
  free(dir);
  if (!result) errno = saved_errno;
  return result;
}

int sm_vfs_open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = (mode_t)va_arg(args, int);
    va_end(args);
  }
  if (!sm_smb_path(path)) return open(path, flags, mode);
  sm_smb_lock();
  size_t index;
  for (index = 0; index < SMB_FD_COUNT && remote_files[index].used; index++)
    ;
  if (index == SMB_FD_COUNT) {
    sm_smb_unlock();
    errno = EMFILE;
    return -1;
  }
  sm_smb_address_t a;
  if (sm_smb_parse_address(path, &a)) {
    sm_smb_unlock();
    return -1;
  }
  sm_vfs_dir_t *owner = find_directory_context(&a);
  struct smb2_context *ctx = owner ? owner->ctx : sm_smb_connect_path(path, &a);
  struct smb2fh *file = ctx ? smb2_open(ctx, a.relative, flags) : NULL;
  if (!file) {
    int error = ctx ? nterror_to_errno(smb2_get_nterror(ctx)) : errno;
    if (!error) error = EIO;
    if (owner) {
      if (error == EIO || error == ETIMEDOUT || error == ECONNRESET)
        invalidate_directory(owner, error);
    } else if (ctx)
      smb2_destroy_context(ctx);
    sm_smb_unlock();
    errno = error;
    return -1;
  }
  remote_files[index] = (remote_file_t){.ctx = owner ? NULL : ctx,
                                        .owner = owner,
                                        .file = file,
                                        .device = remote_device(&a),
                                        .used = 1};
  if (owner) ++owner->users;
  if (flags & (O_CREAT | O_TRUNC)) clear_directory_stat_cache(remote_files[index].device);
  sm_smb_unlock();
  return SMB_FD_BASE + (int)index;
}
int sm_vfs_close(int fd) {
  if (!sm_vfs_remote_fd(fd)) return close(fd);
  int original_errno = errno;
  sm_smb_lock();
  if (fd - SMB_FD_BASE >= SMB_FD_COUNT || !remote_files[fd - SMB_FD_BASE].used) {
    sm_smb_unlock();
    errno = EBADF;
    return -1;
  }
  remote_file_t *f = &remote_files[fd - SMB_FD_BASE];
  int flush_result = wait_file_flush(f);
  int flush_error = errno;
  struct smb2_context *ctx = file_context(f);
  int close_result = ctx ? sm_smb_result(smb2_close(ctx, f->file)) : 0;
  int close_error = errno;
  int result = flush_result < 0 ? -1 : close_result;
  int error = flush_result < 0 ? flush_error : close_error;
  if (f->owner) {
    if (close_result < 0) invalidate_directory(f->owner, close_error);
    --f->owner->users;
    release_directory(f->owner);
  } else if (ctx)
    smb2_destroy_context(ctx);
  memset(f, 0, sizeof(*f));
  sm_smb_unlock();
  errno = result < 0 ? error : original_errno;
  return result;
}
typedef struct smb_io_batch smb_io_batch_t;
typedef struct smb_io_request {
  smb_io_batch_t *batch;
  uint8_t *data;
  uint64_t offset;
  size_t length, completed;
} smb_io_request_t;
struct smb_io_batch {
  remote_file_t *file;
  int write, pending, error;
  bool credit_limited;
  uint8_t *data;
  uint64_t offset, deadline;
  size_t size, next, block;
};

static int smb_io_submit(smb_io_request_t *request);
static int smb_io_next(smb_io_request_t *request) {
  smb_io_batch_t *batch = request->batch;
  if (batch->next >= batch->size) return 0;
  request->data = batch->data + batch->next;
  request->offset = batch->offset + batch->next;
  request->length = batch->size - batch->next;
  if (request->length > batch->block) request->length = batch->block;
  request->completed = 0;
  batch->next += request->length;
  return smb_io_submit(request);
}

static void smb_io_complete(struct smb2_context *ctx, int status, void *data, void *opaque) {
  (void)ctx;
  (void)data;
  smb_io_request_t *request = opaque;
  smb_io_batch_t *batch = request->batch;
  batch->pending--;
  if (status < 0) {
    if (!batch->error) {
      batch->error = -status;
      log_debug("  [SMB] I/O request failed: status=%d detail=%s", -status, smb2_get_error(ctx));
    }
    return;
  }
  if ((size_t)status > request->length - request->completed || (!status && batch->write)) {
    batch->error = EIO;
    return;
  }
  if (status > 0 && (size_t)status < request->length - request->completed)
    batch->credit_limited = true;
  request->completed += (size_t)status;
  batch->deadline = monotonic_time_us() + 5000000;
  if (!status) {
    size_t end = (size_t)(request->offset - batch->offset) + request->completed;
    if (end < batch->size) batch->size = end;
  }
  if (!batch->error && !batch->credit_limited) {
    /* Refill the completed slot immediately instead of draining the whole window. */
    int result = smb_io_next(request);
    if (result < 0) batch->error = -result;
  }
}

static int smb_io_submit(smb_io_request_t *request) {
  smb_io_batch_t *batch = request->batch;
  remote_file_t *f = batch->file;
  uint32_t length = (uint32_t)(request->length - request->completed);
  int result =
      batch->write
          ? smb2_pwrite_async(file_context(f), f->file, request->data + request->completed, length,
                              request->offset + request->completed, smb_io_complete, request)
          : smb2_pread_async(file_context(f), f->file, request->data + request->completed, length,
                             request->offset + request->completed, smb_io_complete, request);
  if (result >= 0) {
    batch->pending++;
  }
  return result;
}

/* Caller owns the SMB lock and buffer until every queued callback completes. */
static ssize_t smb_io(remote_file_t *f, void *data, size_t size, uint64_t offset, int writing) {
  if (!size) return 0;
  if (size > SMB_IO_BLOCK * SMB_IO_REQUESTS) size = SMB_IO_BLOCK * SMB_IO_REQUESTS;
  if (offset > INT64_MAX - size) {
    errno = EOVERFLOW;
    return -1;
  }
  struct smb2_context *ctx = file_context(f);
  uint32_t maximum = writing ? smb2_get_max_write_size(ctx) : smb2_get_max_read_size(ctx);
  /* Keep sequential writes contiguous: splitting each 4 MiB copy buffer into
   * concurrent file extensions substantially slows Windows SMB destinations.
   * Reads retain their smaller rolling window; negotiated limits still apply. */
  size_t limit = writing ? SMB_IO_WRITE_BLOCK : SMB_IO_BLOCK;
  size_t block = maximum < limit ? maximum : limit;
  size_t window = size >= 256 * 1024 ? SMB_IO_REQUESTS : 1;
  if (smb2_get_dialect(ctx) <= SMB2_VERSION_0202 && block > 65536) block = 65536;
  if (!block) {
    errno = EIO;
    return -1;
  }
  smb_io_batch_t batch = {.file = f,
                          .write = writing,
                          .data = data,
                          .offset = offset,
                          .size = size,
                          .block = block,
                          .deadline = monotonic_time_us() + 5000000};
  smb_io_request_t requests[SMB_IO_REQUESTS] = {0};
  for (size_t i = 0; i < window && batch.next < batch.size; ++i) {
    smb_io_request_t *request = &requests[i];
    request->batch = &batch;
    int result = smb_io_next(request);
    if (result < 0) {
      batch.error = -result;
      break;
    }
  }
  for (;;) {
    if (!batch.pending) {
      if (!batch.credit_limited || batch.error) break;
      /* libsmb2 sizes commands from currently available credits. After a short
       * reply, drain queued commands before submitting another: otherwise zero
       * available credits can produce an invalid zero-length request. Complete
       * exact missing ranges serially, without retrying any failed operation. */
      smb_io_request_t *unfinished = NULL;
      for (size_t i = 0; i < window; ++i) {
        smb_io_request_t *request = &requests[i];
        size_t position = (size_t)(request->offset - offset);
        if (!request->length || position >= batch.size) continue;
        size_t needed = batch.size - position;
        if (request->length > needed) request->length = needed;
        if (request->completed < request->length) {
          unfinished = request;
          break;
        }
      }
      int result;
      if (unfinished)
        result = smb_io_submit(unfinished);
      else if (batch.next < batch.size)
        result = smb_io_next(&requests[0]);
      else
        break;
      if (result < 0) {
        batch.error = -result;
        break;
      }
    }
    struct pollfd pfd = {.fd = smb2_get_fd(ctx), .events = smb2_which_events(ctx)};
    int result = poll(&pfd, 1, 1000);
    if (result < 0 && errno == EINTR) continue;
    bool stopping = should_stop_requested() || runtime_sleep_mode_active();
    bool timed_out = monotonic_time_us() >= batch.deadline;
    if (result < 0 || stopping || timed_out || smb2_service(ctx, pfd.revents) < 0) {
      batch.error = stopping ? ECANCELED : timed_out ? ETIMEDOUT : result < 0 ? errno : EIO;
      if (!stopping)
        log_debug("  [SMB] I/O stopped: status=%d detail=%s", batch.error, smb2_get_error(ctx));
      /* Destroy while callbacks still own valid stack state and caller buffers. */
      if (f->owner)
        invalidate_directory(f->owner, batch.error);
      else {
        smb2_destroy_context(ctx);
        f->ctx = NULL;
      }
      f->file = NULL;
      break;
    }
  }
  if (batch.error) {
    errno = batch.error;
    return -1;
  }
  return (ssize_t)batch.size;
}

/* Negative offset selects sequential I/O; pread must leave the cursor unchanged. */
static ssize_t remote_io(int fd, void *data, size_t size, off_t offset, int writing) {
  int sequential = offset < 0;
  sm_smb_lock();
  remote_file_t *f = remote_file(fd);
  if (f && writing && f->flush_state != SMB_FLUSH_NONE) {
    if (wait_file_flush(f) < 0) f = NULL;
    else f->flush_state = SMB_FLUSH_NONE;
  }
  int64_t position = f ? smb2_lseek(file_context(f), f->file, 0, SEEK_CUR, NULL) : -1;
  if (f && position < 0) errno = (int)-position;
  ssize_t result =
      f && position >= 0
          ? smb_io(f, data, size, sequential ? (uint64_t)position : (uint64_t)offset, writing)
          : -1;
  if (f && file_context(f) && position >= 0)
    smb2_lseek(file_context(f), f->file, position + (sequential && result >= 0 ? result : 0),
               SEEK_SET, NULL);
  if (f && writing) clear_directory_stat_cache(f->device);
  sm_smb_unlock();
  return result;
}
ssize_t sm_vfs_read(int fd, void *data, size_t size) {
  return sm_vfs_remote_fd(fd) ? remote_io(fd, data, size, -1, 0) : read(fd, data, size);
}
ssize_t sm_vfs_write(int fd, const void *data, size_t size) {
  return sm_vfs_remote_fd(fd) ? remote_io(fd, (void *)data, size, -1, 1) : write(fd, data, size);
}
ssize_t sm_vfs_pread(int fd, void *data, size_t size, off_t offset) {
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }
  return sm_vfs_remote_fd(fd) ? remote_io(fd, data, size, offset, 0)
                              : pread(fd, data, size, offset);
}
off_t sm_vfs_lseek(int fd, off_t offset, int whence) {
  if (!sm_vfs_remote_fd(fd)) return lseek(fd, offset, whence);
  sm_smb_lock();
  remote_file_t *f = remote_file(fd);
  int64_t result = -1;
  if (f) {
    int64_t base = 0;
    if (whence == SEEK_END) {
      struct smb2_stat_64 st;
      if (sm_smb_result(smb2_fstat(file_context(f), f->file, &st))) goto done;
      if (st.smb2_size > INT64_MAX) { errno = EOVERFLOW; goto done; }
      base = (int64_t)st.smb2_size;
    } else if (whence == SEEK_CUR) {
      base = smb2_lseek(file_context(f), f->file, 0, SEEK_CUR, NULL);
      if (base < 0) { errno = (int)-base; goto done; }
    } else if (whence != SEEK_SET) {
      errno = EINVAL; goto done;
    }
    if (offset < -base) { errno = EINVAL; goto done; }
    if (offset > 0 && base > INT64_MAX - offset) { errno = EOVERFLOW; goto done; }
    result = smb2_lseek(file_context(f), f->file, base + offset, SEEK_SET, NULL);
    if (result < 0) {
      errno = (int)-result;
      result = -1;
    }
  }
done:
  sm_smb_unlock();
  return (off_t)result;
}
int sm_vfs_fsync(int fd) {
  if (!sm_vfs_remote_fd(fd)) return fsync(fd);
  sm_smb_lock();
  remote_file_t *f = remote_file(fd);
  int result = f ? start_file_flush(f) : -1;
  if (!result) result = wait_file_flush(f);
  if (!result) f->flush_state = SMB_FLUSH_NONE;
  sm_smb_unlock();
  return result;
}
int sm_vfs_start_fsync(int fd) {
  if (!sm_vfs_remote_fd(fd)) return fsync(fd);
  sm_smb_lock();
  remote_file_t *file = remote_file(fd);
  int result = file ? start_file_flush(file) : -1;
  sm_smb_unlock();
  return result;
}

typedef int (*path_operation_t)(struct smb2_context *, const char *);
static int remote_path_operation(const char *path, path_operation_t operation) {
  int original_errno = errno;
  sm_smb_lock();
  sm_smb_address_t a;
  if (sm_smb_parse_address(path, &a)) {
    sm_smb_unlock();
    return -1;
  }
  sm_vfs_dir_t *owner = find_directory_context(&a);
  struct smb2_context *ctx = owner ? owner->ctx : sm_smb_connect_path(path, &a);
  int result = ctx ? sm_smb_result(operation(ctx, a.relative)) : -1;
  int error = errno;
  if (!result) clear_directory_stat_cache(remote_device(&a));
  if (result < 0 && ctx && error != ENOENT && error != EEXIST)
    log_debug("  [SMB] path operation failed: path=%s status=%d detail=%s", a.path, error,
               smb2_get_error(ctx));
  if (owner) {
    if (result < 0 && (error == EIO || error == ETIMEDOUT || error == ECONNRESET))
      invalidate_directory(owner, error);
  } else if (ctx)
    smb2_destroy_context(ctx);
  sm_smb_unlock();
  errno = result < 0 ? error : original_errno;
  return result;
}
int sm_vfs_mkdir(const char *path, mode_t mode) {
  return sm_smb_path(path) ? remote_path_operation(path, smb2_mkdir) : mkdir(path, mode);
}
int sm_vfs_rmdir(const char *path) {
  return sm_smb_path(path) ? remote_path_operation(path, smb2_rmdir) : rmdir(path);
}
int sm_vfs_unlink(const char *path) {
  return sm_smb_path(path) ? remote_path_operation(path, smb2_unlink) : unlink(path);
}
int sm_vfs_rename(const char *src, const char *dst) {
  if (!sm_smb_path(src) && !sm_smb_path(dst)) return rename(src, dst);
  if (!sm_smb_path(src) || !sm_smb_path(dst)) {
    errno = EXDEV;
    return -1;
  }
  int original_errno = errno;
  sm_smb_lock();
  sm_smb_address_t a, b;
  if (sm_smb_parse_address(src, &a) || sm_smb_parse_address(dst, &b)) {
    sm_smb_unlock(); return -1;
  }
  if (strcasecmp(a.server, b.server) || strcasecmp(a.share, b.share) ||
      strcmp(a.user, b.user) || strcmp(a.domain, b.domain) || strcmp(a.password, b.password)) {
    sm_smb_unlock(); errno = EXDEV; return -1;
  }
  sm_vfs_dir_t *owner = find_directory_context(&a);
  struct smb2_context *ctx = owner ? owner->ctx : sm_smb_connect_path(src, &a);
  int result = ctx ? sm_smb_result(smb2_rename(ctx, a.relative, b.relative)) : -1;
  int error = errno;
  if (!result) clear_directory_stat_cache(remote_device(&a));
  if (owner) {
    if (result < 0 && (error == EIO || error == ETIMEDOUT || error == ECONNRESET))
      invalidate_directory(owner, error);
  } else if (ctx)
    smb2_destroy_context(ctx);
  sm_smb_unlock();
  errno = result < 0 ? error : original_errno;
  return result;
}
int sm_vfs_access(const char *path, int mode) {
  if (!sm_smb_path(path)) return access(path, mode);
  struct stat st;
  return sm_vfs_stat(path, &st);
}

typedef struct {
  unsigned pending;
  int error;
  bool opened, close_failed;
} smb_access_probe_t;

static void access_open_complete(struct smb2_context *ctx, int status, void *data, void *opaque) {
  (void)ctx;
  smb_access_probe_t *probe = opaque;
  if (status) { probe->error = nterror_to_errno((uint32_t)status); if (!probe->error) probe->error = EIO; }
  else {
    probe->opened = true;
    const struct smb2_create_reply *reply = data;
    if (reply->file_attributes & SMB2_FILE_ATTRIBUTE_READONLY) probe->error = EACCES;
  }
  --probe->pending;
}

static void access_close_complete(struct smb2_context *ctx, int status, void *data, void *opaque) {
  (void)ctx;
  (void)data;
  smb_access_probe_t *probe = opaque;
  probe->close_failed = status != 0;
  if (status && !probe->error) { probe->error = nterror_to_errno((uint32_t)status); if (!probe->error) probe->error = EIO; }
  --probe->pending;
}

/* OPEN + CLOSE requests rights only; no create, write or delete-on-close. */
static int probe_remote_access(struct smb2_context **context, sm_vfs_dir_t *owner,
                                const char *path, uint32_t rights, bool directory) {
  struct smb2_context *ctx = *context;
  smb_access_probe_t probe = {.pending = 2};
  struct smb2_create_request open_request = {
      .impersonation_level = SMB2_IMPERSONATION_IMPERSONATION,
      .desired_access = rights | SMB2_FILE_READ_ATTRIBUTES,
      .share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_WRITE | SMB2_FILE_SHARE_DELETE,
      .create_disposition = SMB2_FILE_OPEN,
      .create_options = directory ? SMB2_FILE_DIRECTORY_FILE : SMB2_FILE_NON_DIRECTORY_FILE,
      .name = path,
  };
  struct smb2_close_request close_request = {0};
  memcpy(close_request.file_id, compound_file_id, sizeof(close_request.file_id));
  struct smb2_pdu *open = smb2_cmd_create_async(ctx, &open_request, access_open_complete, &probe);
  if (!open) { errno = ENOMEM; return -1; }
  struct smb2_pdu *close = smb2_cmd_close_async(ctx, &close_request, access_close_complete, &probe);
  if (!close) { smb2_free_pdu(ctx, open); errno = ENOMEM; return -1; }
  smb2_add_compound_pdu(ctx, open, close);
  smb2_queue_pdu(ctx, open);
  uint64_t deadline = monotonic_time_us() + 5000000;
  int transport_error = 0;
  while (probe.pending) {
    struct pollfd pfd = {.fd = smb2_get_fd(ctx), .events = smb2_which_events(ctx)};
    int result = poll(&pfd, 1, 1000);
    if (result < 0 && errno == EINTR) continue;
    transport_error = result < 0 ? errno : should_stop_requested() || runtime_sleep_mode_active() ? ECANCELED :
                      smb2_service(ctx, pfd.revents) < 0 ? EIO : 0;
    if (!transport_error && probe.pending && monotonic_time_us() >= deadline) transport_error = ETIMEDOUT;
    if (transport_error) break;
  }
  // Context destruction drains callbacks before this stack record expires.
  if (transport_error || (probe.opened && probe.close_failed)) {
    int error = transport_error ? transport_error : probe.error ? probe.error : EIO;
    if (owner) invalidate_directory(owner, error);
    else smb2_destroy_context(ctx);
    *context = NULL;
    probe.error = error;
  }
  if (probe.error) { errno = probe.error; return -1; }
  return 0;
}

int sm_vfs_delete_access(const char *path, bool directory) {
  sm_smb_lock();
  sm_smb_address_t address;
  if (sm_smb_parse_address(path, &address)) { sm_smb_unlock(); return -1; }
  sm_vfs_dir_t *owner = find_directory_context(&address);
  struct smb2_context *ctx = owner ? owner->ctx : sm_smb_connect_path(path, &address);
  if (!ctx) { sm_smb_unlock(); return -1; }
  char parent[MAX_PATH];
  strlcpy(parent, address.relative, sizeof(parent));
  char *slash = strrchr(parent, '/');
  if (slash) *slash = 0;
  else parent[0] = 0;
  int result = probe_remote_access(&ctx, owner, parent, SMB2_FILE_ADD_FILE, true);
  if (!result) {
    result = probe_remote_access(&ctx, owner, address.relative,
                                 SMB2_DELETE | (directory ? SMB2_FILE_ADD_FILE : 0), directory);
  }
  int error = errno;
  if (!owner && ctx) smb2_destroy_context(ctx);
  sm_smb_unlock();
  errno = error;
  return result;
}

int sm_vfs_mkdir_access(const char *path) {
  sm_smb_lock();
  sm_smb_address_t address;
  if (sm_smb_parse_address(path, &address)) { sm_smb_unlock(); return -1; }
  sm_vfs_dir_t *owner = find_directory_context(&address);
  struct smb2_context *ctx = owner ? owner->ctx : sm_smb_connect_path(path, &address);
  int result = ctx ? probe_remote_access(&ctx, owner, address.relative, SMB2_FILE_ADD_SUBDIRECTORY, true) : -1;
  int error = errno;
  if (!owner && ctx) smb2_destroy_context(ctx);
  sm_smb_unlock();
  errno = error;
  return result;
}
int sm_vfs_chmod(const char *path, mode_t mode) {
  if (!sm_smb_path(path)) return chmod(path, mode);
  errno = ENOTSUP;
  return -1;
}
int sm_vfs_fchmod(int fd, mode_t mode) {
  if (!sm_vfs_remote_fd(fd)) return fchmod(fd, mode);
  errno = ENOTSUP;
  return -1;
}

char *sm_vfs_realpath(const char *path, char *out) {
  if (!sm_smb_path(path)) return realpath(path, out);
  sm_smb_address_t address;
  if (sm_smb_parse_address(path, &address)) return NULL;
  struct stat st;
  if (sm_vfs_stat(address.path, &st)) return NULL;
  if (!out) return strdup(address.path);
  strcpy(out, address.path);
  return out;
}
int sm_vfs_fdatasync(int fd) {
  return sm_vfs_remote_fd(fd) ? sm_vfs_fsync(fd) : fdatasync(fd);
}
int sm_vfs_fadvise(int fd, off_t offset, off_t length, int advice) {
  return sm_vfs_remote_fd(fd) ? 0 : posix_fadvise(fd, offset, length, advice);
}
