#define WFM_VFS_IMPLEMENTATION
#include "vfs.h"
#include "smb.h"
#include "path_util.h"
#include "transfer.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SMB_FD_BASE 0x40000000
#define SMB_FD_COUNT 256
#define SMB_IO_REQUESTS 8
#define SMB_IO_BLOCK (1024 * 1024)
typedef struct remote_file {
  struct smb2_context *ctx;
  struct smb2fh *file;
  dev_t device;
  int used;
  uint64_t transfer_size;
#if WFM_DEBUG
  unsigned long long read_bytes, write_bytes, requests, io_ns;
#endif
} remote_file_t;
static remote_file_t remote_files[SMB_FD_COUNT];
struct wfm_dir {
  DIR *native;
  struct smb2_context *ctx;
  struct smb2dir *remote;
  struct dirent entry;
  struct smb2dirent *last;
  int error;
  dev_t device;
  char path[PATH_MAX];
  struct wfm_dir *next;
};
static wfm_dir_t *remote_dirs;

int wfm_fd_remote(int fd) { return fd >= SMB_FD_BASE; }
static remote_file_t *remote_file(int fd) {
  if(fd < SMB_FD_BASE || fd - SMB_FD_BASE >= SMB_FD_COUNT ||
     !remote_files[fd - SMB_FD_BASE].used) { errno = EBADF; return NULL; }
  if(!remote_files[fd - SMB_FD_BASE].ctx) { errno = EIO; return NULL; }
  return &remote_files[fd - SMB_FD_BASE];
}

void wfm_set_transfer_size(int fd, uint64_t size) {
  if(!wfm_fd_remote(fd)) return;
  smb_lock();
  remote_file_t *f = remote_file(fd);
  if(f) f->transfer_size = size;
  smb_unlock();
}

static uint32_t path_hash(const char *path, size_t length) {
  uint32_t hash = 2166136261u;
  for(size_t i = 0; i < length; i++) hash = (hash ^ (unsigned char)path[i]) * 16777619u;
  return hash;
}
static dev_t remote_device(const smb_address_t *a) {
  return (dev_t)(0x80000000u | (path_hash(a->server, strlen(a->server)) ^
                               path_hash(a->share, strlen(a->share))));
}
static void convert_stat(const struct smb2_stat_64 *in, dev_t device, struct stat *out) {
  memset(out, 0, sizeof(*out));
  out->st_dev = device;
  out->st_ino = in->smb2_ino;
  out->st_nlink = in->smb2_nlink ? in->smb2_nlink : 1;
  out->st_mode = (in->smb2_type == SMB2_TYPE_DIRECTORY ? S_IFDIR :
                 in->smb2_type == SMB2_TYPE_LINK ? S_IFLNK : S_IFREG) | 0777;
  out->st_size = (off_t)in->smb2_size;
  out->st_atim.tv_sec = (time_t)in->smb2_atime;
  out->st_atim.tv_nsec = (long)in->smb2_atime_nsec;
  out->st_mtim.tv_sec = (time_t)in->smb2_mtime;
  out->st_mtim.tv_nsec = (long)in->smb2_mtime_nsec;
  out->st_ctim.tv_sec = (time_t)in->smb2_ctime;
  out->st_ctim.tv_nsec = (long)in->smb2_ctime_nsec;
}

int wfm_stat(const char *path, struct stat *st) {
  if(!smb_path(path)) return stat(path, st);
  smb_lock();
  char parent[PATH_MAX];
  if(!path_dirname(path, parent, sizeof(parent))) {
    for(wfm_dir_t *dir = remote_dirs; dir; dir = dir->next) {
      if(!strcmp(dir->path, parent) && dir->last &&
         !strcmp(dir->last->name, path_basename(path))) {
        convert_stat(&dir->last->st, dir->device, st);
        smb_unlock();
        return 0;
      }
    }
  }
  smb_address_t a;
  struct smb2_context *ctx = smb_connect_path(path, &a);
  struct smb2_stat_64 remote;
  int result = ctx ? smb_result(smb2_stat(ctx, a.relative, &remote)) : -1;
  int error = errno;
  if(!result) convert_stat(&remote, remote_device(&a), st);
  if(ctx) smb2_destroy_context(ctx);
  smb_unlock();
  errno = error;
  return result;
}
int wfm_lstat(const char *path, struct stat *st) {
  return smb_path(path) ? wfm_stat(path, st) : lstat(path, st);
}
int wfm_fstat(int fd, struct stat *st) {
  if(!wfm_fd_remote(fd)) return fstat(fd, st);
  smb_lock();
  remote_file_t *f = remote_file(fd);
  struct smb2_stat_64 remote;
  int result = f ? smb_result(smb2_fstat(f->ctx, f->file, &remote)) : -1;
  if(!result) {
    convert_stat(&remote, f->device, st);
    f->transfer_size = remote.smb2_size;
  }
  smb_unlock();
  return result;
}
int wfm_statvfs(const char *path, struct statvfs *st) {
  if(!smb_path(path)) return statvfs(path, st);
  smb_lock();
  smb_address_t a;
  struct smb2_context *ctx = smb_connect_path(path, &a);
  struct smb2_statvfs remote;
  int result = ctx ? smb_result(smb2_statvfs(ctx, a.relative, &remote)) : -1;
  int error = errno;
  if(!result) {
    memset(st, 0, sizeof(*st));
    st->f_bsize = remote.f_bsize; st->f_frsize = remote.f_frsize;
    st->f_blocks = remote.f_blocks; st->f_bfree = remote.f_bfree; st->f_bavail = remote.f_bavail;
    st->f_files = remote.f_files; st->f_ffree = remote.f_ffree;
    st->f_favail = remote.f_favail; st->f_flag = remote.f_flag;
    st->f_namemax = remote.f_namemax;
  }
  if(ctx) smb2_destroy_context(ctx);
  smb_unlock();
  errno = error;
  return result;
}

wfm_dir_t *wfm_opendir(const char *path) {
  wfm_dir_t *dir = calloc(1, sizeof(*dir));
  if(!dir) return NULL;
  if(!smb_path(path)) {
    if((dir->native = opendir(path))) return dir;
    free(dir); return NULL;
  }
  smb_lock();
  smb_address_t a;
  dir->ctx = smb_connect_path(path, &a);
  if(dir->ctx) dir->remote = smb2_opendir(dir->ctx, a.relative);
  if(!dir->remote) {
    int error = dir->ctx ? nterror_to_errno(smb2_get_nterror(dir->ctx)) : errno;
    if(!error) error = EIO;
    if(dir->ctx) smb2_destroy_context(dir->ctx);
    free(dir);
    smb_unlock(); errno = error; return NULL;
  }
  strcpy(dir->path, a.path);
  dir->device = remote_device(&a);
  dir->next = remote_dirs;
  remote_dirs = dir;
  smb_unlock();
  return dir;
}
struct dirent *wfm_readdir(wfm_dir_t *dir) {
  if(dir->error) { errno = dir->error; return NULL; }
  if(dir->native) {
    errno = 0;
    struct dirent *entry = readdir(dir->native);
    if(!entry) dir->error = errno;
    return entry;
  }
  smb_lock();
  dir->last = smb2_readdir(dir->ctx, dir->remote);
  if(!dir->last) { smb_unlock(); return NULL; }
  if(strlen(dir->last->name) >= sizeof(dir->entry.d_name)) {
    dir->last = NULL; dir->error = ENAMETOOLONG;
    smb_unlock(); errno = dir->error; return NULL;
  }
  memset(&dir->entry, 0, sizeof(dir->entry));
  strcpy(dir->entry.d_name, dir->last->name);
  dir->entry.d_ino = dir->last->st.smb2_ino;
  dir->entry.d_type = dir->last->st.smb2_type == SMB2_TYPE_DIRECTORY ? DT_DIR : DT_REG;
  smb_unlock();
  return &dir->entry;
}
int wfm_dir_error(wfm_dir_t *dir) {
  if(dir->error) errno = dir->error;
  return dir->error;
}
int wfm_closedir(wfm_dir_t *dir) {
  int saved_errno = errno;
  int result = 0;
  if(dir->native) result = closedir(dir->native);
  else {
    smb_lock();
    wfm_dir_t **p = &remote_dirs;
    while(*p && *p != dir) p = &(*p)->next;
    if(*p) *p = dir->next;
    smb2_closedir(dir->ctx, dir->remote);
    smb2_destroy_context(dir->ctx);
    smb_unlock();
  }
  free(dir);
  if(!result) errno = saved_errno;
  return result;
}

int wfm_open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if(flags & O_CREAT) { va_list args; va_start(args, flags); mode = (mode_t)va_arg(args, int); va_end(args); }
  if(!smb_path(path)) return open(path, flags, mode);
  smb_lock();
  size_t index;
  for(index = 0; index < SMB_FD_COUNT && remote_files[index].used; index++);
  if(index == SMB_FD_COUNT) { smb_unlock(); errno = EMFILE; return -1; }
  smb_address_t a;
  struct smb2_context *ctx = smb_connect_path(path, &a);
  struct smb2fh *file = ctx ? smb2_open(ctx, a.relative, flags) : NULL;
  if(!file) {
    int error = ctx ? nterror_to_errno(smb2_get_nterror(ctx)) : errno;
    if(!error) error = EIO;
    if(ctx) smb2_destroy_context(ctx);
    smb_unlock(); errno = error; return -1;
  }
  remote_files[index] = (remote_file_t){.ctx = ctx, .file = file, .device = remote_device(&a), .used = 1};
  /* libsmb2 caches the size returned by open; these seeks do not send requests. */
  int64_t position = smb2_lseek(ctx, file, 0, SEEK_CUR, NULL);
  int64_t size = smb2_lseek(ctx, file, 0, SEEK_END, NULL);
  smb2_lseek(ctx, file, position >= 0 ? position : 0, SEEK_SET, NULL);
  if(size >= 0) remote_files[index].transfer_size = (uint64_t)size;
  smb_unlock();
  return SMB_FD_BASE + (int)index;
}
int wfm_close(int fd) {
  if(!wfm_fd_remote(fd)) return close(fd);
  int original_errno = errno;
  smb_lock();
  remote_file_t *f = remote_file(fd);
  if(!f && fd >= SMB_FD_BASE && fd - SMB_FD_BASE < SMB_FD_COUNT && remote_files[fd - SMB_FD_BASE].used) {
    memset(&remote_files[fd - SMB_FD_BASE], 0, sizeof(*remote_files));
    smb_unlock(); errno = original_errno; return 0;
  }
  int result = f ? smb_result(smb2_close(f->ctx, f->file)) : -1;
  int error = errno;
  if(f) {
#if WFM_DEBUG
    fprintf(stderr, "SMB IO fd=%d read=%llu write=%llu bytes requests=%llu wait=%.3f s window=%d\n",
            fd, f->read_bytes, f->write_bytes, f->requests, f->io_ns / 1000000000.0,
            transfer_use_pipeline(f->transfer_size) ? SMB_IO_REQUESTS : 1);
#endif
    smb2_destroy_context(f->ctx); memset(f, 0, sizeof(*f));
  }
  smb_unlock(); errno = result < 0 ? error : original_errno;
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
};

static int smb_io_submit(smb_io_request_t *request);
static void smb_io_complete(struct smb2_context *ctx, int status, void *data, void *opaque) {
  (void)ctx; (void)data;
  smb_io_request_t *request = opaque;
  smb_io_batch_t *batch = request->batch;
  batch->pending--;
  if(status < 0) { if(!batch->error) batch->error = -status; return; }
  if((size_t)status > request->length - request->completed || (!status && batch->write)) {
    batch->error = EIO; return;
  }
  request->completed += (size_t)status;
  /* Credits may shorten a request; finish its exact range before advancing. */
  if(status && request->completed < request->length && !batch->error) {
    int result = smb_io_submit(request);
    if(result < 0) batch->error = -result;
  }
}

static int smb_io_submit(smb_io_request_t *request) {
  smb_io_batch_t *batch = request->batch;
  remote_file_t *f = batch->file;
  uint32_t length = (uint32_t)(request->length - request->completed);
  int result = batch->write ?
    smb2_pwrite_async(f->ctx, f->file, request->data + request->completed, length,
                      request->offset + request->completed, smb_io_complete, request) :
    smb2_pread_async(f->ctx, f->file, request->data + request->completed, length,
                     request->offset + request->completed, smb_io_complete, request);
  if(result >= 0) {
    batch->pending++;
#if WFM_DEBUG
    f->requests++;
#endif
  }
  return result;
}

/* Caller owns the SMB lock and buffer until every queued callback completes. */
static ssize_t smb_io(remote_file_t *f, void *data, size_t size, uint64_t offset, int writing) {
  if(!size) return 0;
  uint32_t maximum = writing ? smb2_get_max_write_size(f->ctx) : smb2_get_max_read_size(f->ctx);
  size_t block = maximum < SMB_IO_BLOCK ? maximum : SMB_IO_BLOCK;
  size_t window = transfer_use_pipeline(f->transfer_size) ? SMB_IO_REQUESTS : 1;
  if(smb2_get_dialect(f->ctx) <= SMB2_VERSION_0202 && block > 65536) block = 65536;
  if(window > 1 && size <= SMB_IO_BLOCK && block > 256 * 1024) block = 256 * 1024;
  if(!block) { errno = EIO; return -1; }
  if(size > block * window) size = block * window;
  smb_io_batch_t batch = {.file = f, .write = writing};
  smb_io_request_t requests[SMB_IO_REQUESTS] = {0};
  size_t count = 0;
#if WFM_DEBUG
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  for(size_t position = 0; position < size; position += block) {
    smb_io_request_t *request = &requests[count++];
    *request = (smb_io_request_t){.batch = &batch, .data = (uint8_t *)data + position,
      .offset = offset + position, .length = size - position < block ? size - position : block};
    int result = smb_io_submit(request);
    if(result < 0) { batch.error = -result; break; }
  }
  while(batch.pending) {
    struct pollfd pfd = {.fd = smb2_get_fd(f->ctx), .events = smb2_which_events(f->ctx)};
    int result = poll(&pfd, 1, 1000);
    if(result < 0 && errno == EINTR) continue;
    if(result < 0 || smb2_service(f->ctx, pfd.revents) < 0) {
      batch.error = result < 0 ? errno : EIO;
#if WFM_DEBUG
      fprintf(stderr, "SMB %s transport failed: %s\n", writing ? "write" : "read", smb2_get_error(f->ctx));
#endif
      /* Destroy while callbacks still own valid stack state and caller buffers. */
      smb2_destroy_context(f->ctx);
      f->ctx = NULL; f->file = NULL;
      break;
    }
  }
#if WFM_DEBUG
  clock_gettime(CLOCK_MONOTONIC, &end);
  f->io_ns += (unsigned long long)((end.tv_sec - start.tv_sec) * 1000000000LL + end.tv_nsec - start.tv_nsec);
#endif
  if(batch.error) {
#if WFM_DEBUG
    fprintf(stderr, "SMB %s offset=%llu size=%zu failed: %s\n",
            writing ? "write" : "read", (unsigned long long)offset, size, strerror(batch.error));
#endif
    errno = batch.error; return -1;
  }
  size_t completed = 0;
  for(size_t i = 0; i < count; i++) {
    completed += requests[i].completed;
    if(requests[i].completed < requests[i].length) break;
  }
  if(writing && offset + completed > f->transfer_size) f->transfer_size = offset + completed;
#if WFM_DEBUG
  if(writing) f->write_bytes += completed;
  else f->read_bytes += completed;
#endif
  return (ssize_t)completed;
}

/* Negative offset selects sequential I/O; pread must leave the cursor unchanged. */
static ssize_t remote_io(int fd, void *data, size_t size, off_t offset, int writing) {
  int sequential = offset < 0;
  smb_lock();
  remote_file_t *f = remote_file(fd);
  int64_t position = f ? smb2_lseek(f->ctx, f->file, 0, SEEK_CUR, NULL) : -1;
  if(f && position < 0) errno = (int)-position;
  ssize_t result = f && position >= 0 ?
    smb_io(f, data, size, sequential ? (uint64_t)position : (uint64_t)offset, writing) : -1;
  if(f && f->ctx && position >= 0)
    smb2_lseek(f->ctx, f->file, position + (sequential && result >= 0 ? result : 0), SEEK_SET, NULL);
  smb_unlock();
  return result;
}
ssize_t wfm_read(int fd, void *data, size_t size) {
  return wfm_fd_remote(fd) ? remote_io(fd, data, size, -1, 0) : read(fd, data, size);
}
ssize_t wfm_write(int fd, const void *data, size_t size) {
  return wfm_fd_remote(fd) ? remote_io(fd, (void *)data, size, -1, 1) : write(fd, data, size);
}
ssize_t wfm_pread(int fd, void *data, size_t size, off_t offset) {
  if(offset < 0) { errno = EINVAL; return -1; }
  return wfm_fd_remote(fd) ? remote_io(fd, data, size, offset, 0) : pread(fd, data, size, offset);
}
off_t wfm_lseek(int fd, off_t offset, int whence) {
  if(!wfm_fd_remote(fd)) return lseek(fd, offset, whence);
  smb_lock();
  remote_file_t *f = remote_file(fd);
  int64_t result = -1;
  if(f) {
    if(whence == SEEK_END) {
      struct smb2_stat_64 st;
      if(smb_result(smb2_fstat(f->ctx, f->file, &st))) goto done;
      offset += (off_t)st.smb2_size;
      whence = SEEK_SET;
    }
    result = smb2_lseek(f->ctx, f->file, offset, whence, NULL);
    if(result < 0) { errno = (int)-result; result = -1; }
  }
done:
  smb_unlock();
  return (off_t)result;
}
int wfm_fsync(int fd) {
  if(!wfm_fd_remote(fd)) return fsync(fd);
  smb_lock();
  remote_file_t *f = remote_file(fd);
  int result = f ? smb_result(smb2_fsync(f->ctx, f->file)) : -1;
  smb_unlock(); return result;
}

typedef int (*path_operation_t)(struct smb2_context *, const char *);
static int remote_path_operation(const char *path, path_operation_t operation) {
  int original_errno = errno;
  smb_lock();
  smb_address_t a;
  struct smb2_context *ctx = smb_connect_path(path, &a);
  int result = ctx ? smb_result(operation(ctx, a.relative)) : -1;
  int error = errno;
  if(ctx) smb2_destroy_context(ctx);
  smb_unlock(); errno = result < 0 ? error : original_errno;
  return result;
}
int wfm_mkdir(const char *path, mode_t mode) {
  return smb_path(path) ? remote_path_operation(path, smb2_mkdir) : mkdir(path, mode);
}
int wfm_rmdir(const char *path) {
  return smb_path(path) ? remote_path_operation(path, smb2_rmdir) : rmdir(path);
}
int wfm_unlink(const char *path) {
  return smb_path(path) ? remote_path_operation(path, smb2_unlink) : unlink(path);
}
int wfm_rename(const char *src, const char *dst) {
  if(!smb_path(src) && !smb_path(dst)) return rename(src, dst);
  if(!smb_path(src) || !smb_path(dst)) { errno = EXDEV; return -1; }
  int original_errno = errno;
  smb_lock();
  smb_address_t a, b;
  struct smb2_context *ctx = smb_connect_path(src, &a);
  int result = -1;
  if(ctx && !smb_parse_address(dst, &b)) {
    if(strcmp(a.server, b.server) || strcmp(a.share, b.share)) errno = EXDEV;
    else result = smb_result(smb2_rename(ctx, a.relative, b.relative));
  }
  int error = errno;
  if(ctx) smb2_destroy_context(ctx);
  smb_unlock(); errno = result < 0 ? error : original_errno;
  return result;
}
int wfm_access(const char *path, int mode) {
  if(!smb_path(path)) return access(path, mode);
  struct stat st;
  return wfm_stat(path, &st);
}
int wfm_chmod(const char *path, mode_t mode) {
  if(!smb_path(path)) return chmod(path, mode);
  errno = ENOTSUP; return -1;
}
int wfm_fchmod(int fd, mode_t mode) {
  if(!wfm_fd_remote(fd)) return fchmod(fd, mode);
  errno = ENOTSUP; return -1;
}
