#include "sm_platform.h"

#include <dirent.h>

#include "sm_limits.h"
#include "sm_path_utils.h"
#include "sm_runtime.h"
#include "sm_storage.h"
#include "sm_smb.h"
#include "sm_vfs.h"
#ifdef SM_VFS_ENABLED
#include <pthread.h>
#include "sm_log.h"
#include "sm_time.h"
#endif

#define STORAGE_COPY_BUFFER_SIZE (4u * 1024u * 1024u)
#define STORAGE_PENDING_FILES 4u

static bool valid_storage_path(const char *path) {
  if (sm_smb_path(path)) {
    char canonical[MAX_PATH];
    return sm_smb_normalize(path, canonical) == 0;
  }
  if (!path || path[0] != '/' || path[1] == '\0')
    return false;
  for (const char *component = path + 1; *component != '\0';) {
    const char *end = strchr(component, '/');
    size_t length = end ? (size_t)(end - component) : strlen(component);
    if (length == 0 || (length == 1 && component[0] == '.') ||
        (length == 2 && component[0] == '.' && component[1] == '.')) {
      return false;
    }
    if (!end)
      break;
    if (end[1] == '\0')
      return false;
    component = end + 1;
  }
  return true;
}

static bool valid_storage_transfer_paths(const char *source,
                                          const char *destination) {
  if (!valid_storage_path(source) || !valid_storage_path(destination) ||
      path_matches_root_or_child(destination, source) ||
      (sm_smb_path(source) && sm_smb_path(destination) && sm_smb_path_within(destination, source))) {
    errno = EINVAL;
    return false;
  }

  char parent[MAX_PATH];
  if (strlcpy(parent, destination, sizeof(parent)) >= sizeof(parent)) {
    errno = ENAMETOOLONG;
    return false;
  }
  char *separator = strrchr(parent, '/');
  if (separator == parent)
    separator[1] = '\0';
  else
    *separator = '\0';

  // The destination does not exist yet. Resolve its parent to also catch
  // aliases that would recursively copy a directory into itself.
  char resolved_source[PATH_MAX];
  char resolved_parent[PATH_MAX];
  if (!realpath(source, resolved_source) || !realpath(parent, resolved_parent))
    return false;
  if (path_matches_root_or_child(resolved_parent, resolved_source)) {
    errno = EINVAL;
    return false;
  }
  return true;
}

static bool storage_operation_cancelled(sm_storage_cancel_fn cancel,
                                        void *ctx) {
  return should_stop_requested() || runtime_sleep_mode_active() ||
         (cancel && cancel(ctx));
}

static int join_storage_path(const char *parent, const char *name,
                             char out[MAX_PATH]) {
  int written = snprintf(out, MAX_PATH, "%s/%s", parent, name);
  if (written < 0 || (size_t)written >= MAX_PATH) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

static int measure_storage_path(const char *path, uint64_t *size,
                                uint64_t *file_count,
                                sm_storage_cancel_fn cancel, void *ctx,
                                bool allow_non_regular) {
  if (storage_operation_cancelled(cancel, ctx)) {
    errno = ECANCELED;
    return -1;
  }

  struct stat st;
  if (lstat(path, &st) != 0)
    return -1;
  if (!S_ISDIR(st.st_mode)) {
    if (!S_ISREG(st.st_mode) && !allow_non_regular) {
      errno = EINVAL;
      return -1;
    }
    if (S_ISREG(st.st_mode)) {
      uint64_t file_size = st.st_size > 0 ? (uint64_t)st.st_size : 0;
      if (UINT64_MAX - *size < file_size) {
        errno = EOVERFLOW;
        return -1;
      }
      *size += file_size;
    }
    if (*file_count == UINT64_MAX) {
      errno = EOVERFLOW;
      return -1;
    }
    (*file_count)++;
    return 0;
  }
  DIR *dir = opendir(path);
  if (!dir)
    return -1;
  int result = 0;
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir);
    if (!entry) {
      if (errno != 0)
        result = -1;
      break;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char child[MAX_PATH];
    if (join_storage_path(path, entry->d_name, child) != 0 ||
        measure_storage_path(child, size, file_count, cancel, ctx,
                             allow_non_regular) != 0) {
      result = -1;
      break;
    }
  }
  int saved_errno = result != 0 ? errno : 0;
  if (closedir(dir) != 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
  if (result != 0)
    errno = saved_errno;
  return result;
}

int sm_storage_measure_path(const char *path, uint64_t *size_out) {
  uint64_t file_count = 0;
  return sm_storage_measure_path_progress(path, size_out, &file_count, NULL,
                                          NULL);
}

int sm_storage_measure_path_progress(const char *path, uint64_t *size_out,
                                     uint64_t *file_count_out,
                                     sm_storage_cancel_fn cancel, void *ctx) {
  if (!path || !size_out || !file_count_out) {
    errno = EINVAL;
    return -1;
  }
  *size_out = 0;
  *file_count_out = 0;
  return measure_storage_path(path, size_out, file_count_out, cancel, ctx,
                              false);
}

int sm_storage_measure_delete_path_progress(
    const char *path, uint64_t *size_out, uint64_t *file_count_out,
    sm_storage_cancel_fn cancel, void *ctx) {
  if (!path || !size_out || !file_count_out) {
    errno = EINVAL;
    return -1;
  }
  *size_out = 0;
  *file_count_out = 0;
  return measure_storage_path(path, size_out, file_count_out, cancel, ctx, true);
}

static int write_storage_buffer(int fd, const void *buffer, size_t size,
                                sm_storage_progress_fn progress, sm_storage_cancel_fn cancel,
                                void *ctx) {
  size_t written = 0;
  while (written < size) {
    if (storage_operation_cancelled(cancel, ctx)) {
      errno = ECANCELED;
      return -1;
    }
    ssize_t count = write(fd, (const unsigned char *)buffer + written, size - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      if (!count) errno = EIO;
      return -1;
    }
    written += (size_t)count;
    if (progress) progress((uint64_t)count, 0, ctx);
  }
  return 0;
}

static size_t storage_read_size(off_t source_size, uint64_t completed, size_t capacity) {
  if (source_size < 0 || completed > (uint64_t)source_size) return capacity;
  uint64_t remaining = (uint64_t)source_size - completed;
  // Verify EOF with one byte, rather than a full SMB request window. If the
  // source grew, subsequent reads return to normal streaming until actual EOF.
  if (!remaining) return 1;
  return remaining < capacity ? (size_t)remaining : capacity;
}

#ifdef SM_VFS_ENABLED
typedef struct {
  uint64_t bytes, data_us, read_us, write_us, wait_us;
} storage_copy_stats_t;

typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int source_fd;
  void *buffers[2];
  ssize_t sizes[2];
  int errors[2];
  unsigned pending;
  bool stop;
  off_t source_size;
  size_t chunk_size;
  uint64_t read_us;
} storage_read_ahead_t;

static void *storage_read_ahead_thread(void *opaque) {
  storage_read_ahead_t *state = opaque;
  unsigned slot = 0;
  uint64_t completed = 0;
  for (;;) {
    pthread_mutex_lock(&state->mutex);
    while (state->pending == 2 && !state->stop)
      pthread_cond_wait(&state->cond, &state->mutex);
    bool stop = state->stop;
    pthread_mutex_unlock(&state->mutex);
    if (stop) break;

    uint64_t started = monotonic_time_us();
    ssize_t count;
    do {
      size_t size = storage_read_size(state->source_size, completed, state->chunk_size);
      count = read(state->source_fd, state->buffers[slot], size);
    } while (count < 0 && errno == EINTR && !storage_operation_cancelled(NULL, NULL));
    int error = count < 0 ? errno : 0;
    state->read_us += monotonic_time_us() - started;
    if (count > 0) completed += (uint64_t)count;

    pthread_mutex_lock(&state->mutex);
    if (state->stop) {
      pthread_mutex_unlock(&state->mutex);
      break;
    }
    state->sizes[slot] = count;
    state->errors[slot] = error;
    ++state->pending;
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
    if (count <= 0) break;
    slot ^= 1;
  }
  return NULL;
}

/* 1: copied, 0: use sequential fallback, -1: transfer failed. State and buffers
 * belong to this call; the reader is joined before either descriptor is closed. */
static int copy_file_read_ahead(int source_fd, int destination_fd, off_t source_size, void *buffer,
                                sm_storage_progress_fn progress, sm_storage_cancel_fn cancel,
                                void *ctx, storage_copy_stats_t *stats) {
  size_t chunk_size =
      source_size < 2 * STORAGE_COPY_BUFFER_SIZE ? 1024u * 1024u : STORAGE_COPY_BUFFER_SIZE;
  while (chunk_size > 256u * 1024u && (uint64_t)source_size < 2 * chunk_size)
    chunk_size /= 2;
  void *second = malloc(chunk_size);
  if (!second) return 0;
  storage_read_ahead_t state = {.source_fd = source_fd,
                                .buffers = {buffer, second},
                                .source_size = source_size,
                                .chunk_size = chunk_size};
  if (pthread_mutex_init(&state.mutex, NULL)) {
    free(second);
    return 0;
  }
  if (pthread_cond_init(&state.cond, NULL)) {
    pthread_mutex_destroy(&state.mutex);
    free(second);
    return 0;
  }
  pthread_t reader;
  uint64_t started = monotonic_time_us(), write_us = 0, wait_us = 0, bytes = 0;
  if (pthread_create(&reader, NULL, storage_read_ahead_thread, &state)) {
    pthread_cond_destroy(&state.cond);
    pthread_mutex_destroy(&state.mutex);
    free(second);
    return 0;
  }
  unsigned slot = 0;
  int status = 0;
  for (;;) {
    if (storage_operation_cancelled(cancel, ctx)) {
      status = ECANCELED;
      break;
    }
    uint64_t waiting = monotonic_time_us();
    pthread_mutex_lock(&state.mutex);
    while (!state.pending)
      pthread_cond_wait(&state.cond, &state.mutex);
    ssize_t count = state.sizes[slot];
    int error = state.errors[slot];
    pthread_mutex_unlock(&state.mutex);
    wait_us += monotonic_time_us() - waiting;
    if (count <= 0) {
      status = count < 0 ? error : 0;
      break;
    }
    uint64_t writing = monotonic_time_us();
    int result = write_storage_buffer(destination_fd, state.buffers[slot], (size_t)count, progress,
                                      cancel, ctx);
    if (result) status = errno;
    write_us += monotonic_time_us() - writing;
    if (status) break;
    bytes += (uint64_t)count;
    /* Keep this slot occupied while writing, then let the reader reuse it. */
    pthread_mutex_lock(&state.mutex);
    --state.pending;
    pthread_cond_signal(&state.cond);
    pthread_mutex_unlock(&state.mutex);
    slot ^= 1;
  }
  pthread_mutex_lock(&state.mutex);
  state.stop = true;
  pthread_cond_signal(&state.cond);
  pthread_mutex_unlock(&state.mutex);
  pthread_join(reader, NULL);
  pthread_cond_destroy(&state.cond);
  pthread_mutex_destroy(&state.mutex);
  free(second);
  *stats = (storage_copy_stats_t){.bytes = bytes,
                                  .data_us = monotonic_time_us() - started,
                                  .read_us = state.read_us,
                                  .write_us = write_us,
                                  .wait_us = wait_us};
  if (status) {
    errno = status;
    return -1;
  }
  return 1;
}
#endif

typedef struct storage_pending_file storage_pending_file_t;
typedef struct storage_pending_files storage_pending_files_t;
#ifdef SM_VFS_ENABLED
struct storage_pending_file {
  int fd;
  storage_copy_stats_t stats;
  uint64_t started, opened, queued, source_close_us;
};
struct storage_pending_files {
  storage_pending_file_t files[STORAGE_PENDING_FILES];
  unsigned count;
};

static void log_storage_copy(const storage_copy_stats_t *stats, uint64_t started,
                             uint64_t opened, uint64_t flush_us, uint64_t close_us,
                             uint64_t deferred_us, int status) {
  if (stats->bytes < 32u * 1024u * 1024u) return;
  log_debug(
      "  [SMB] copy pipeline: bytes=%llu elapsed_ms=%llu data_ms=%llu read_ms=%llu "
      "write_ms=%llu wait_ms=%llu open_ms=%llu flush_ms=%llu close_ms=%llu deferred_ms=%llu status=%d",
      (unsigned long long)stats->bytes,
      (unsigned long long)((monotonic_time_us() - started) / 1000),
      (unsigned long long)(stats->data_us / 1000), (unsigned long long)(stats->read_us / 1000),
      (unsigned long long)(stats->write_us / 1000), (unsigned long long)(stats->wait_us / 1000),
      (unsigned long long)((opened - started) / 1000),
      (unsigned long long)(flush_us / 1000), (unsigned long long)(close_us / 1000),
      (unsigned long long)(deferred_us / 1000), status);
}

/* Always release a file before closing its directory or removing its tree. */
static int finish_pending_file(storage_pending_file_t *pending, int status,
                                sm_storage_progress_fn progress, sm_storage_cancel_fn cancel,
                                void *ctx) {
  if (!pending || pending->fd < 0) return 0;
  int fd = pending->fd;
  pending->fd = -1;
  if (!status && storage_operation_cancelled(cancel, ctx)) status = ECANCELED;
  uint64_t started = monotonic_time_us();
  if (!status && fdatasync(fd) != 0) status = errno;
  uint64_t flushed = monotonic_time_us();
  if (close(fd) != 0 && !status) status = errno;
  uint64_t closed = monotonic_time_us();
  if (!status && progress) progress(0, 1, ctx);
  log_storage_copy(&pending->stats, pending->started, pending->opened,
                   flushed - started, closed - flushed + pending->source_close_us,
                   started - pending->queued, status);
  if (status) {
    errno = status;
    return -1;
  }
  return 0;
}

static int finish_oldest_file(storage_pending_files_t *pending, int status,
                               sm_storage_progress_fn progress, sm_storage_cancel_fn cancel,
                               void *ctx) {
  if (!pending || !pending->count) return 0;
  int result = finish_pending_file(&pending->files[0], status, progress, cancel, ctx);
  int error = errno;
  --pending->count;
  // Flush callbacks own VFS registry slots, not these movable timing records.
  memmove(pending->files, pending->files + 1, pending->count * sizeof(pending->files[0]));
  if (result) errno = error;
  return result;
}

static int finish_pending_files(storage_pending_files_t *pending, int status,
                                sm_storage_progress_fn progress, sm_storage_cancel_fn cancel,
                                void *ctx) {
  if (!pending || !pending->count) return 0;
  while (pending->count)
    if (finish_oldest_file(pending, status, progress, cancel, ctx) != 0 && !status) status = errno;
  if (status) {
    errno = status;
    return -1;
  }
  return 0;
}
#endif

static int copy_regular_file(const char *source, const char *destination, mode_t mode,
                             off_t source_size, void *buffer, sm_storage_progress_fn progress,
                             sm_storage_cancel_fn cancel, void *ctx, storage_pending_files_t *pending) {
#ifndef SM_VFS_ENABLED
  (void)pending;
#endif
#ifdef SM_VFS_ENABLED
  bool smb_copy = sm_smb_path(source) || sm_smb_path(destination);
  uint64_t started = smb_copy ? monotonic_time_us() : 0;
  storage_copy_stats_t stats = {0};
#endif
  int source_fd = open(source, O_RDONLY);
  if (source_fd < 0) return -1;
  (void)posix_fadvise(source_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
  bool smb_download = sm_smb_path(source) && !sm_smb_path(destination);
  int destination_fd = open(destination, O_WRONLY | O_CREAT | O_EXCL,
                            smb_download ? 0777 : mode & 0777);
  if (destination_fd < 0) {
    int saved_errno = errno;
    (void)close(source_fd);
    errno = saved_errno;
    return -1;
  }

  int result = 0;
  int pipelined = 0;
#ifdef SM_VFS_ENABLED
  uint64_t opened = smb_copy ? monotonic_time_us() : 0;
  if (source_size >= 512u * 1024u &&
      sm_vfs_remote_fd(source_fd) != sm_vfs_remote_fd(destination_fd)) {
    pipelined = copy_file_read_ahead(source_fd, destination_fd, source_size, buffer, progress,
                                     cancel, ctx, &stats);
    if (pipelined < 0) result = -1;
  }
#endif
  uint64_t completed = 0;
  while (pipelined == 0 && result == 0) {
    if (storage_operation_cancelled(cancel, ctx)) {
      errno = ECANCELED;
      result = -1;
      break;
    }
    size_t size = storage_read_size(source_size, completed, STORAGE_COPY_BUFFER_SIZE);
    ssize_t read_size = read(source_fd, buffer, size);
    if (read_size < 0) {
      if (errno == EINTR) continue;
      result = -1;
      break;
    }
    if (read_size == 0) break;
    completed += (uint64_t)read_size;
    result = write_storage_buffer(destination_fd, buffer, (size_t)read_size, progress, cancel, ctx);
  }
  // Apply explicitly: open's mode is filtered by the process umask.
  if (!result && smb_download && fchmod(destination_fd, 0777) != 0) result = -1;
#ifdef SM_VFS_ENABLED
  // A short sliding window spans alternating large data/small metadata files.
  // Every flush is checked before releasing the pinned directory session.
  if (!result && pending && sm_vfs_remote_fd(destination_fd)) {
    if (sm_vfs_start_fsync(destination_fd) != 0) result = -1;
    uint64_t queued = monotonic_time_us();
    uint64_t source_close_us = 0;
    if (!result) {
      uint64_t closing = monotonic_time_us();
      if (close(source_fd) != 0) result = -1;
      source_close_us = monotonic_time_us() - closing;
      source_fd = -1;
    }
    if (!result) {
      pending->files[pending->count++] = (storage_pending_file_t){
          .fd = destination_fd, .stats = stats, .started = started, .opened = opened,
          .queued = queued, .source_close_us = source_close_us};
      return pending->count == STORAGE_PENDING_FILES
                 ? finish_oldest_file(pending, 0, progress, cancel, ctx) : 0;
    }
  }
#endif
  int saved_errno = result != 0 ? errno : 0;
#ifdef SM_VFS_ENABLED
  uint64_t flush_started = smb_copy ? monotonic_time_us() : 0;
#endif
  if (result == 0 && fdatasync(destination_fd) != 0) {
    result = -1;
    saved_errno = errno;
  }
#ifdef SM_VFS_ENABLED
  uint64_t flushed = smb_copy ? monotonic_time_us() : 0;
#endif
  if (close(destination_fd) != 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
  if (source_fd >= 0 && close(source_fd) != 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
  if (result != 0) {
    if (unlink(destination) != 0 && errno != ENOENT) saved_errno = errno;
  } else if (progress) {
    progress(0, 1, ctx);
  }
#ifdef SM_VFS_ENABLED
  log_storage_copy(&stats, started, opened, flushed - flush_started,
                   monotonic_time_us() - flushed, 0, result ? saved_errno : 0);
#endif
  if (result != 0) errno = saved_errno;
  return result;
}

static int delete_storage_path(const char *path, sm_storage_progress_fn progress, void *ctx);

static int copy_storage_path(const char *source, const char *destination,
                             void *buffer,
                             sm_storage_progress_fn progress,
                             sm_storage_cancel_fn cancel, void *ctx, storage_pending_files_t *pending) {
  if (storage_operation_cancelled(cancel, ctx)) {
    errno = ECANCELED;
    return -1;
  }

  struct stat st;
  if (lstat(source, &st) != 0)
    return -1;
  if (S_ISREG(st.st_mode))
    return copy_regular_file(source, destination, st.st_mode, st.st_size, buffer, progress,
                             cancel, ctx, pending);
  if (!S_ISDIR(st.st_mode)) {
    errno = EINVAL;
    return -1;
  }
#ifdef SM_VFS_ENABLED
  if (finish_pending_files(pending, 0, progress, cancel, ctx) != 0) return -1;
#endif
  if (mkdir(destination, st.st_mode & 0777) != 0)
    return -1;

  DIR *dir = opendir(source);
  if (!dir) {
    int saved_errno = errno;
    (void)rmdir(destination);
    errno = saved_errno;
    return -1;
  }
  // Keep a destination session available for child creates. Listing permission
  // is optional: write-only shares still use the ordinary per-file connection.
#ifdef SM_VFS_ENABLED
  DIR *destination_dir = sm_smb_path(destination) ? opendir(destination) : NULL;
  storage_pending_files_t child_pending = {0};
  pending = destination_dir ? &child_pending : NULL;
#endif
  int result = 0;
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir);
    if (!entry) {
      if (errno != 0)
        result = -1;
      break;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char source_child[MAX_PATH];
    char destination_child[MAX_PATH];
    if (join_storage_path(source, entry->d_name, source_child) != 0 ||
        join_storage_path(destination, entry->d_name, destination_child) != 0 ||
        copy_storage_path(source_child, destination_child, buffer, progress,
                          cancel, ctx, pending) != 0) {
      result = -1;
      break;
    }
  }
  int saved_errno = result != 0 ? errno : 0;
#ifdef SM_VFS_ENABLED
  if (finish_pending_files(pending, saved_errno, progress, cancel, ctx) != 0 && !result) {
    result = -1;
    saved_errno = errno;
  }
#endif
  if (closedir(dir) != 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
#ifdef SM_VFS_ENABLED
  if (destination_dir) (void)closedir(destination_dir);
#endif
  if (result != 0) {
    if (delete_storage_path(destination, NULL, NULL) != 0 && errno != ENOENT)
      saved_errno = errno;
    errno = saved_errno;
  }
  return result;
}

int sm_storage_copy_path(const char *source, const char *destination) {
  return sm_storage_copy_path_progress(source, destination, NULL, NULL, NULL);
}

int sm_storage_copy_path_progress(const char *source, const char *destination,
                                  sm_storage_progress_fn progress,
                                  sm_storage_cancel_fn cancel, void *ctx) {
  if (!valid_storage_transfer_paths(source, destination)) {
    return -1;
  }
  struct stat st;
  if (lstat(destination, &st) == 0) {
    errno = EEXIST;
    return -1;
  }
  if (errno != ENOENT)
    return -1;
  void *buffer = malloc(STORAGE_COPY_BUFFER_SIZE);
  if (!buffer) {
    errno = ENOMEM;
    return -1;
  }
  int result = copy_storage_path(source, destination, buffer, progress, cancel,
                                 ctx, NULL);
  int saved_errno = result != 0 ? errno : 0;
  free(buffer);
  if (result != 0)
    errno = saved_errno;
  return result;
}

static int delete_storage_path(const char *path,
                               sm_storage_progress_fn progress, void *ctx) {
  struct stat st;
  if (lstat(path, &st) != 0)
    return -1;
  if (!S_ISDIR(st.st_mode)) {
    if (unlink(path) != 0)
      return -1;
    if (progress) {
      uint64_t size = S_ISREG(st.st_mode) && st.st_size > 0
                          ? (uint64_t)st.st_size
                          : 0;
      progress(size, 1, ctx);
    }
    return 0;
  }

  DIR *dir = opendir(path);
  if (!dir)
    return -1;
  int result = 0;
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(dir);
    if (!entry) {
      if (errno != 0)
        result = -1;
      break;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char child[MAX_PATH];
    if (join_storage_path(path, entry->d_name, child) != 0 ||
        delete_storage_path(child, progress, ctx) != 0) {
      result = -1;
      break;
    }
  }
  int saved_errno = result != 0 ? errno : 0;
  if (closedir(dir) != 0 && result == 0) {
    result = -1;
    saved_errno = errno;
  }
  if (result == 0 && rmdir(path) != 0) {
    result = -1;
    saved_errno = errno;
  }
  if (result != 0)
    errno = saved_errno;
  return result;
}

int sm_storage_replace_directory_progress(const char *source, const char *destination,
                                          sm_storage_progress_fn progress,
                                          sm_storage_cancel_fn cancel,
                                          sm_storage_finalize_fn begin_finalize,
                                          void *ctx) {
  if (!valid_storage_transfer_paths(source, destination)) return -1;
  if (path_matches_root_or_child(source, destination)) {
    errno = EINVAL;
    return -1;
  }
  struct stat st;
  if (lstat(source, &st) != 0) return -1;
  if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
  if (lstat(destination, &st) == 0) {
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    // Resolve aliases before copying: replacing an ancestor of the physical
    // source would remove the source while cleaning up the previous tree.
    char resolved_source[PATH_MAX], resolved_destination[PATH_MAX];
    if (!realpath(source, resolved_source) || !realpath(destination, resolved_destination))
      return -1;
    if (path_matches_root_or_child(resolved_source, resolved_destination)) {
      errno = EINVAL;
      return -1;
    }
  } else if (errno != ENOENT) return -1;
  char staging[MAX_PATH], fresh[MAX_PATH], previous[MAX_PATH];
  if (strlcpy(staging, destination, sizeof(staging)) >= sizeof(staging)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  char *name = strrchr(staging, '/');
  *name = 0;
  if (join_storage_path(staging[0] ? staging : "/", ".backport-XXXXXX", fresh) != 0)
    return -1;
  (void)strlcpy(staging, fresh, sizeof(staging));
  if (!mkdtemp(staging)) return -1;
  int status = 0;
  bool keep_previous = false;
  if (join_storage_path(staging, "new", fresh) != 0 ||
      join_storage_path(staging, "old", previous) != 0 ||
      sm_storage_copy_path_progress(source, fresh, progress, cancel, ctx) != 0) {
    status = errno ? errno : EIO;
  } else if (storage_operation_cancelled(cancel, ctx) ||
             (begin_finalize && !begin_finalize(ctx))) {
    status = ECANCELED;
  } else {
    // Only the short commit step is non-cancellable. Keep the previous tree
    // until the complete new copy has taken its final name.
    if (lstat(destination, &st) == 0) {
      if (!S_ISDIR(st.st_mode)) status = ENOTDIR;
      else if (rename(destination, previous) != 0) status = errno;
      else keep_previous = true;
    } else if (errno != ENOENT) status = errno;
    if (status == 0 && rename(fresh, destination) != 0) {
      status = errno;
      if (keep_previous && rename(previous, destination) == 0)
        keep_previous = false;
    } else if (status == 0) {
      keep_previous = false;
    }
  }
  // If rollback itself failed, retain the backup for recovery. Cleanup is
  // otherwise unconditional, including during shutdown, like copy cleanup.
  if (!keep_previous && delete_storage_path(staging, NULL, NULL) != 0 && status == 0)
    status = errno ? errno : EIO;
  if (status != 0) { errno = status; return -1; }
  return 0;
}

int sm_storage_delete_path(const char *path) {
  return sm_storage_delete_path_progress(path, NULL, NULL);
}

int sm_storage_delete_path_progress(const char *path,
                                    sm_storage_progress_fn progress,
                                    void *ctx) {
  if (!valid_storage_path(path)) {
    errno = EINVAL;
    return -1;
  }
  if (storage_operation_cancelled(NULL, NULL)) {
    errno = ECANCELED;
    return -1;
  }
  // Once recursive deletion starts it must finish. Cancelling in the middle
  // would intentionally leave the only source tree partially removed.
  return delete_storage_path(path, progress, ctx);
}

int sm_storage_move_path(const char *source, const char *destination) {
  return sm_storage_move_path_progress(source, destination, NULL, NULL, NULL,
                                       NULL, NULL);
}

int sm_storage_move_path_progress(const char *source, const char *destination,
                                  sm_storage_progress_fn progress,
                                  sm_storage_cancel_fn cancel,
                                  sm_storage_finalize_fn begin_finalize,
                                  void *ctx,
                                  bool *renamed_out) {
  if (!valid_storage_transfer_paths(source, destination)) {
    return -1;
  }
  if (renamed_out)
    *renamed_out = false;
  if (storage_operation_cancelled(cancel, ctx)) {
    errno = ECANCELED;
    return -1;
  }
  struct stat st;
  if (lstat(source, &st) != 0)
    return -1;
  if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
    errno = EINVAL;
    return -1;
  }
  if (lstat(destination, &st) == 0) {
    errno = EEXIST;
    return -1;
  }
  if (errno != ENOENT)
    return -1;
  if (rename(source, destination) == 0) {
    if (renamed_out)
      *renamed_out = true;
    return 0;
  }
  if (errno != EXDEV)
    return -1;
  if (sm_storage_copy_path_progress(source, destination, progress, cancel,
                                    ctx) != 0)
    return -1;
  if (storage_operation_cancelled(cancel, ctx) ||
      (begin_finalize && !begin_finalize(ctx)) ||
      storage_operation_cancelled(NULL, NULL)) {
    int saved_errno = ECANCELED;
    if (delete_storage_path(destination, NULL, NULL) != 0 && errno != ENOENT)
      saved_errno = errno;
    errno = saved_errno;
    return -1;
  }
  if (sm_storage_delete_path(source) == 0)
    return 0;
  // Keep the complete destination copy if source cleanup failed. Removing it
  // could leave only a partially deleted source and lose recoverable data.
  return -1;
}
