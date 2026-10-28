#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "filemgr_internal.h"
#include "json_util.h"
#include "path_util.h"
#include "vfs.h"
#include "websrv.h"
#include "transfer.h"

#define DOWNLOAD_ARCHIVE_NAME_LIMIT 20

typedef struct tar_frame {
  char path[PATH_MAX];
  char name[PATH_MAX];
  DIR *dir;
  int header_sent;
  struct tar_frame *next;
} tar_frame_t;

typedef struct tar_stream {
  file_task_t *task;
  char **paths;
  size_t path_count;
  size_t path_index;
  size_t stack_depth;
  tar_frame_t *stack;
  int fd;
  transfer_reader_t *reader;
  uint64_t file_position;
  char current_file[PATH_MAX];
  unsigned long long file_remaining;
  size_t file_padding;
  size_t pad_remaining;
  char *pending;
  size_t pending_size;
  size_t pending_offset;
  int final_blocks;
  int done;
  int error;
} tar_stream_t;

typedef struct download_file_stream {
  file_task_t *task;
  int fd;
  transfer_reader_t *reader;
  int reader_initialized;
  unsigned long long size;
  unsigned long long sent;
  int done;
  int error;
} download_file_stream_t;

static enum MHD_Result
download_task_request_error(struct MHD_Connection *conn, file_task_t *task,
                            char **paths, size_t count, unsigned int status,
                            const char *msg) {
  free_paths(paths, count);
  free_task(task);
  return send_json_error(conn, status, msg);
}

static int
tar_checksum(char *header) {
  int sum = 0;
  int i;

  memset(header + 148, ' ', 8);
  for(i = 0; i < 512; i++) {
    sum += (unsigned char)header[i];
  }
  snprintf(header + 148, 8, "%06o", sum);
  header[154] = 0;
  header[155] = ' ';
  return 0;
}

static int
tar_split_name(const char *name, char *out_name, size_t out_name_size,
               char *prefix, size_t prefix_size) {
  size_t len = strlen(name);
  const char *slash;

  memset(out_name, 0, out_name_size);
  memset(prefix, 0, prefix_size);
  if(len < out_name_size) {
    strcpy(out_name, name);
    return 0;
  }
  for(slash = name + len; slash > name; slash--) {
    size_t prefix_len;
    size_t name_len;

    if(*slash != '/') {
      continue;
    }
    prefix_len = (size_t)(slash - name);
    name_len = len - prefix_len - 1;
    if(prefix_len < prefix_size && name_len > 0 && name_len < out_name_size) {
      memcpy(prefix, name, prefix_len);
      prefix[prefix_len] = 0;
      memcpy(out_name, slash + 1, name_len + 1);
      return 0;
    }
  }
  errno = ENAMETOOLONG;
  return -1;
}

static int
tar_queue_header(tar_stream_t *s, const char *name, const struct stat *st,
                 char type) {
  char header[512];
  char tar_name[100];
  char prefix[155];
  unsigned int mode = (unsigned int)(st->st_mode & 07777);
  unsigned long long size = type == '0' ? (unsigned long long)st->st_size : 0;
  long long mtime = (long long)st->st_mtime;

  if(tar_split_name(name, tar_name, sizeof(tar_name), prefix, sizeof(prefix))) {
    return -1;
  }
  memset(header, 0, sizeof(header));
  memcpy(header, tar_name, strlen(tar_name));
  snprintf(header + 100, 8, "%07o", mode);
  snprintf(header + 108, 8, "%07o", 0);
  snprintf(header + 116, 8, "%07o", 0);
  snprintf(header + 124, 12, "%011llo", size);
  snprintf(header + 136, 12, "%011llo", (unsigned long long)mtime);
  header[156] = type;
  memcpy(header + 257, "ustar", 5);
  memcpy(header + 263, "00", 2);
  if(prefix[0]) {
    memcpy(header + 345, prefix, strlen(prefix));
  }
  tar_checksum(header);
  s->pending = malloc(sizeof(header));
  if(!s->pending) {
    errno = ENOMEM;
    return -1;
  }
  memcpy(s->pending, header, sizeof(header));
  s->pending_size = sizeof(header);
  s->pending_offset = 0;
  return 0;
}

static int
tar_push_dir(tar_stream_t *s, const char *path, const char *name) {
  tar_frame_t *frame = calloc(1, sizeof(*frame));

  if(!frame) {
    errno = ENOMEM;
    return -1;
  }
  snprintf(frame->path, sizeof(frame->path), "%s", path);
  snprintf(frame->name, sizeof(frame->name), "%s", name);
  frame->dir = opendir(path);
  if(!frame->dir) {
    int error = errno;
    free(frame);
    errno = error;
    return -1;
  }
  frame->next = s->stack;
  s->stack = frame;
  s->stack_depth++;
  return 0;
}

static void
tar_pop_dir(tar_stream_t *s) {
  tar_frame_t *frame = s->stack;

  if(!frame) {
    return;
  }
  s->stack = frame->next;
  if(s->stack_depth) {
    s->stack_depth--;
  }
  if(frame->dir) {
    closedir(frame->dir);
  }
  free(frame);
}

static int
tar_start_path(tar_stream_t *s, const char *path, const char *name) {
  struct stat st;

  if(lstat(path, &st)) {
    return -1;
  }
  if(S_ISDIR(st.st_mode)) {
    char dir_name[PATH_MAX];
    snprintf(dir_name, sizeof(dir_name), "%s%s", name,
             name[strlen(name) - 1] == '/' ? "" : "/");
    return tar_push_dir(s, path, dir_name);
  }
  if(!S_ISREG(st.st_mode)) {
    errno = ENOTSUP;
    return -1;
  }
  if(tar_queue_header(s, name, &st, '0')) {
    return -1;
  }
  s->fd = open(path, O_RDONLY);
  if(s->fd < 0) {
    return -1;
  }
  snprintf(s->current_file, sizeof(s->current_file), "%s", path);
  s->file_remaining = (unsigned long long)st.st_size;
  s->file_position = 0;
  s->reader = transfer_http_reader_start(s->fd, 0, s->file_remaining, s->file_remaining, s->task);
  s->file_padding = (size_t)((512 - ((unsigned long long)st.st_size % 512)) % 512);
  if(!s->file_remaining) {
    close(s->fd);
    s->fd = -1;
    s->current_file[0] = 0;
  }
  return 0;
}

static int
tar_prepare_next(tar_stream_t *s) {
  while(!s->pending && s->fd < 0 && !s->done) {
    if(s->task && task_cancel_requested(s->task)) {
      errno = ECANCELED;
      return -1;
    }
    if(s->pad_remaining) {
      size_t size = s->pad_remaining > 512 ? 512 : s->pad_remaining;
      s->pending = calloc(1, size);
      if(!s->pending) {
        errno = ENOMEM;
        return -1;
      }
      s->pending_size = size;
      s->pending_offset = 0;
      s->pad_remaining -= size;
      return 0;
    }
    if(s->stack) {
      tar_frame_t *frame = s->stack;
      struct dirent *entry;
      struct stat st;

      if(!frame->header_sent) {
        frame->header_sent = 1;
        if(lstat(frame->path, &st) ||
           tar_queue_header(s, frame->name, &st, '5')) {
          return -1;
        }
        return 0;
      }
      while((entry = readdir(frame->dir))) {
        char child[PATH_MAX];
        char child_name[PATH_MAX];

        if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
          continue;
        }
        if(path_join(child, sizeof(child), frame->path, entry->d_name) ||
           path_join(child_name, sizeof(child_name), frame->name,
                     entry->d_name) ||
           tar_start_path(s, child, child_name)) {
          return -1;
        }
        return 0;
      }
      if(wfm_dir_error(frame->dir)) return -1;
      tar_pop_dir(s);
      continue;
    }
    if(s->path_index < s->path_count) {
      const char *path = s->paths[s->path_index++];
      if(tar_start_path(s, path, path_basename(path))) {
        return -1;
      }
      continue;
    }
    if(s->final_blocks < 2) {
      s->pending = calloc(1, 512);
      if(!s->pending) {
        errno = ENOMEM;
        return -1;
      }
      s->pending_size = 512;
      s->pending_offset = 0;
      s->final_blocks++;
      return 0;
    }
    s->done = 1;
  }
  return 0;
}

static ssize_t
tar_read(void *cls, uint64_t pos, char *buf, size_t max) {
  tar_stream_t *s = cls;
  size_t out = 0;
  unsigned long long progress = 0;
  char progress_current[PATH_MAX] = {0};
  (void)pos;

  while(out < max && !s->done) {
    if(s->task && task_cancel_requested(s->task)) {
      s->error = ECANCELED;
      return out ? (ssize_t)out : MHD_CONTENT_READER_END_WITH_ERROR;
    }
    if(tar_prepare_next(s)) {
      s->error = errno ? errno : EIO;
      return out ? (ssize_t)out : MHD_CONTENT_READER_END_WITH_ERROR;
    }
    if(s->pending) {
      size_t left = s->pending_size - s->pending_offset;
      size_t take = left < max - out ? left : max - out;
      memcpy(buf + out, s->pending + s->pending_offset, take);
      s->pending_offset += take;
      out += take;
      if(s->pending_offset >= s->pending_size) {
        free(s->pending);
        s->pending = NULL;
        s->pending_size = 0;
        s->pending_offset = 0;
      }
      continue;
    }
    if(s->fd >= 0) {
      size_t want = max - out;
      ssize_t n;
      if((unsigned long long)want > s->file_remaining) {
        want = (size_t)s->file_remaining;
      }
      n = transfer_read_at(s->reader, s->fd, buf + out, want, s->file_position);
      if(n < 0) {
        s->error = errno;
        return out ? (ssize_t)out : MHD_CONTENT_READER_END_WITH_ERROR;
      }
      if(!n) {
        s->error = EIO;
        return out ? (ssize_t)out : MHD_CONTENT_READER_END_WITH_ERROR;
      }
      out += (size_t)n;
      s->file_remaining -= (unsigned long long)n;
      s->file_position += (unsigned long long)n;
      progress += (unsigned long long)n;
      if(s->current_file[0]) {
        snprintf(progress_current, sizeof(progress_current), "%s",
                 s->current_file);
      }
      if(!s->file_remaining) {
        transfer_reader_close(s->reader);
        s->reader = NULL;
        close(s->fd);
        s->fd = -1;
        s->current_file[0] = 0;
        s->pad_remaining = s->file_padding;
        s->file_padding = 0;
      }
    }
  }
  if(s->task && progress) {
    task_update(s->task, TASK_RUNNING,
                progress_current[0] ? progress_current : NULL,
                progress, NULL);
  }
  return out ? (ssize_t)out : MHD_CONTENT_READER_END_OF_STREAM;
}

static void
tar_close(void *cls) {
  tar_stream_t *s = cls;
  int canceled;

  if(!s) {
    return;
  }
  transfer_reader_close(s->reader);
  if(s->fd >= 0) {
    close(s->fd);
  }
  while(s->stack) {
    tar_pop_dir(s);
  }
  canceled = s->task && task_cancel_requested(s->task);
  if(s->task) {
    char current[PATH_MAX];
    snprintf(current, sizeof(current), "%s",
             s->task->current[0] ? s->task->current : s->task->src);
    if(canceled) {
      task_update(s->task, TASK_CANCELED, current, 0, "canceled");
    } else if(s->error) {
      errno = s->error;
      task_update(s->task, TASK_FAILED, current, 0, strerror(errno));
    } else if(s->done) {
      task_update(s->task, TASK_DONE, current, 0, NULL);
    } else {
      task_update(s->task, TASK_FAILED, current, 0, "client disconnected");
    }
  }
  free(s->pending);
  free_paths(s->paths, s->path_count);
  free(s);
}

static int
prepare_download_task(file_task_t *task) {
  unsigned long long total = 0;
  size_t file_count = 0;
  size_t dir_count = 0;
  size_t i;

  task_update(task, TASK_RUNNING, "preparing", 0, NULL);
  for(i = 0; i < task->src_count; i++) {
    if(count_task_path_bytes(task, task->srcs[i], task->srcs[i],
                             &total, &file_count, &dir_count)) {
      return -1;
    }
  }
  pthread_mutex_lock(&g_tasks_lock);
  task->total = total;
  task->file_count = file_count;
  task->dir_count = dir_count;
  task->updated_at = time(NULL);
  pthread_mutex_unlock(&g_tasks_lock);
  return 0;
}

static size_t
append_truncated_name(char *out, size_t size, size_t len, const char *name,
                      size_t limit) {
  while(*name && len < limit && len + 1 < size) {
    unsigned char c = (unsigned char)*name;
    out[len++] = *name++;
    if(c >= 0x80) {
      while((*name & 0xc0) == 0x80 && len < limit && len + 1 < size) {
        out[len++] = *name++;
      }
    }
  }
  out[len] = 0;
  return len;
}

static void
download_archive_name(char *out, size_t size, char **paths, size_t count) {
  char name[PATH_MAX];
  size_t len = 0;
  size_t i;

  out[0] = 0;
  if(count == 1) {
    path_basename_copy(paths[0], name, sizeof(name));
    append_truncated_name(out, size, 0, name, DOWNLOAD_ARCHIVE_NAME_LIMIT);
  } else {
    for(i = 0; i < count && len < DOWNLOAD_ARCHIVE_NAME_LIMIT; i++) {
      path_basename_copy(paths[i], name, sizeof(name));
      if(i && len + 1 < DOWNLOAD_ARCHIVE_NAME_LIMIT && len + 1 < size) {
        out[len++] = ' ';
        out[len] = 0;
      }
      len = append_truncated_name(out, size, len, name,
                                  DOWNLOAD_ARCHIVE_NAME_LIMIT);
    }
  }
  if(!out[0]) {
    snprintf(out, size, "download");
  }
  snprintf(out + strlen(out), size - strlen(out), ".tar");
}

static void
download_file_name(char *out, size_t size, const char *path) {
  path_basename_copy(path, out, size);
  if(!out[0]) {
    snprintf(out, size, "download");
  }
}

static size_t
header_quoted_filename(char *out, size_t size, const char *name) {
  size_t len = 0;

  while(*name && len + 1 < size) {
    unsigned char c = (unsigned char)*name++;
    if(c < 0x20 || c == 0x7f || c == '"' || c == '\\') {
      c = '_';
    }
    out[len++] = (char)c;
  }
  out[len] = 0;
  return len;
}

static size_t
header_percent_filename(char *out, size_t size, const char *name) {
  static const char hex[] = "0123456789ABCDEF";
  size_t len = 0;

  while(*name && len + 1 < size) {
    unsigned char c = (unsigned char)*name++;
    int safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
    if(safe) {
      out[len++] = (char)c;
    } else {
      if(len + 3 >= size) {
        break;
      }
      out[len++] = '%';
      out[len++] = hex[c >> 4];
      out[len++] = hex[c & 15];
    }
  }
  out[len] = 0;
  return len;
}

static void
add_download_filename_header(struct MHD_Response *resp, const char *name) {
  char quoted[PATH_MAX];
  char encoded[PATH_MAX * 3];
  char header[PATH_MAX * 4];

  header_quoted_filename(quoted, sizeof(quoted), name);
  header_percent_filename(encoded, sizeof(encoded), name);
  snprintf(header, sizeof(header),
           "attachment; filename=\"%s\"; filename*=UTF-8''%s",
           quoted, encoded);
  MHD_add_response_header(resp, "Content-Disposition", header);
}

static enum MHD_Result
create_download_task_response(struct MHD_Connection *conn, char **paths,
                              size_t count) {
  file_task_t *task = calloc(1, sizeof(file_task_t));
  strbuf_t b = {0};
  struct stat st;
  size_t i;

  if(!task) {
    free_paths(paths, count);
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  }
  if(!count) {
    return download_task_request_error(conn, task, paths, count,
                                       MHD_HTTP_BAD_REQUEST, "no source paths");
  }
  for(i = 0; i < count; i++) {
    if(lstat(paths[i], &st)) {
      return download_task_request_error(conn, task, paths, count,
                                         MHD_HTTP_NOT_FOUND, "file not found");
    }
    if(!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
      return download_task_request_error(conn, task, paths, count,
                                         MHD_HTTP_BAD_REQUEST, "invalid path");
    }
  }
  task->op = TASK_DOWNLOAD;
  task->state = TASK_QUEUED;
  task->srcs = paths;
  task->src_count = count;
  snprintf(task->src, sizeof(task->src), "%s%s", paths[0],
           count > 1 ? " ..." : "");
  snprintf(task->current, sizeof(task->current), "%s", task->src);
  task->created_at = time(NULL);
  task->updated_at = task->created_at;

  pthread_mutex_lock(&g_tasks_lock);
  remove_finished_tasks_locked();
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    return download_task_request_error(conn, task, paths, count,
                                       MHD_HTTP_CONFLICT, "another task is running");
  }
  task->id = g_next_task_id++;
  task->next = g_tasks;
  g_tasks = task;
  pthread_mutex_unlock(&g_tasks_lock);

  strbuf_printf(&b, "{\"ok\":true,\"task_id\":%lu}", task->id);
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}

enum MHD_Result
api_download_prepare(struct MHD_Connection *conn, const char *body,
                     size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char **paths = NULL;
  size_t count = 0;

  if(!paths_raw || parse_paths(paths_raw, &paths, &count)) {
    free(paths_raw);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  free(paths_raw);
  return create_download_task_response(conn, paths, count);
}

static ssize_t
download_file_read(void *cls, uint64_t pos, char *buf, size_t max) {
  download_file_stream_t *s = cls;
  ssize_t len;

  if(task_cancel_requested(s->task)) {
    s->error = ECANCELED;
    return MHD_CONTENT_READER_END_WITH_ERROR;
  }
  if(pos >= s->size) { s->done = 1; return MHD_CONTENT_READER_END_OF_STREAM; }
  if(max > s->size - pos) max = (size_t)(s->size - pos);
  if(!s->reader_initialized) {
    s->reader = transfer_http_reader_start(s->fd, pos, s->size - pos, s->size, s->task);
    s->reader_initialized = 1;
  }
  len = transfer_read_at(s->reader, s->fd, buf, max, pos);
  if(len < 0) {
    s->error = errno ? errno : EIO;
    return MHD_CONTENT_READER_END_WITH_ERROR;
  }
  if(!len) {
    s->error = EIO;
    return MHD_CONTENT_READER_END_WITH_ERROR;
  }
  {
    unsigned long long end = (unsigned long long)pos + (unsigned long long)len;
    unsigned long long add = end > s->sent ? end - s->sent : 0;
    if(end > s->sent) {
      s->sent = end;
    }
    if(s->sent >= s->size) {
      s->done = 1;
    }
    task_update(s->task, TASK_RUNNING, s->task->src, add, NULL);
  }
  return len;
}

static void
download_file_close(void *cls) {
  download_file_stream_t *s = cls;
  int canceled;

  if(!s) {
    return;
  }
  transfer_reader_close(s->reader);
  if(s->fd >= 0) {
    close(s->fd);
  }
  canceled = task_cancel_requested(s->task);
  if(canceled) {
    task_update(s->task, TASK_CANCELED, s->task->src, 0, "canceled");
  } else if(s->error) {
    errno = s->error;
    task_update(s->task, TASK_FAILED, s->task->src, 0, strerror(errno));
  } else if(s->done) {
    task_update(s->task, TASK_DONE, s->task->src, 0, NULL);
  } else {
    task_update(s->task, TASK_FAILED, s->task->src, 0, "client disconnected");
  }
  free(s);
}

enum MHD_Result
api_download(struct MHD_Connection *conn) {
  char *idstr = query_value(conn, "id");
  unsigned long id = idstr ? strtoul(idstr, NULL, 10) : 0;
  char **paths = NULL;
  size_t count = 0;
  struct stat st;
  tar_stream_t *stream;
  struct MHD_Response *resp;
  enum MHD_Result ret;
  file_task_t *task;
  char download_name[PATH_MAX];

  free(idstr);
  pthread_mutex_lock(&g_tasks_lock);
  task = find_task_locked(id);
  if(!task || task->op != TASK_DOWNLOAD || task->state != TASK_QUEUED) {
    pthread_mutex_unlock(&g_tasks_lock);
    return send_json_error(conn, MHD_HTTP_NOT_FOUND, "active task not found");
  }
  task->state = TASK_RUNNING;
  task->updated_at = time(NULL);
  pthread_mutex_unlock(&g_tasks_lock);

  if(prepare_download_task(task)) {
    char current[PATH_MAX];
    snprintf(current, sizeof(current), "%s",
             task->current[0] ? task->current : task->src);
    if(errno == ECANCELED || task_cancel_requested(task)) {
      task_update(task, TASK_CANCELED, current, 0, "canceled");
    } else {
      task_update(task, TASK_FAILED, current, 0, strerror(errno));
    }
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, NULL);
  }

  paths = task->srcs;
  count = task->src_count;
  if(count == 1 && !stat(paths[0], &st) && S_ISREG(st.st_mode)) {
    download_file_stream_t *file_stream = calloc(1, sizeof(*file_stream));
    if(!file_stream) {
      task_update(task, TASK_FAILED, task->src, 0, "out of memory");
      return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
    }
    file_stream->task = task;
    file_stream->fd = -1;
    file_stream->size = (unsigned long long)st.st_size;
    file_stream->done = file_stream->size == 0;
    file_stream->fd = open(paths[0], O_RDONLY);
    if(file_stream->fd < 0) {
      free(file_stream);
      task_update(task, TASK_FAILED, task->src, 0, strerror(errno));
      return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
    }
    resp = MHD_create_response_from_callback((uint64_t)st.st_size,
                                             file_stream->size < TRANSFER_HTTP_BUFFER_SIZE ?
                                               (file_stream->size ? (size_t)file_stream->size : 1) : TRANSFER_HTTP_BUFFER_SIZE,
                                             download_file_read, file_stream,
                                             download_file_close);
    if(!resp) {
      download_file_close(file_stream);
      return MHD_NO;
    }
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                            "application/octet-stream");
    download_file_name(download_name, sizeof(download_name), paths[0]);
    add_download_filename_header(resp, download_name);
    ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
  }
  if(!(stream = calloc(1, sizeof(*stream)))) {
    task_update(task, TASK_FAILED, task->src, 0, "out of memory");
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  }
  stream->fd = -1;
  stream->task = task;
  stream->paths = NULL;
  stream->path_count = count;
  stream->paths = calloc(count, sizeof(char *));
  if(!stream->paths) {
    tar_close(stream);
    task_update(task, TASK_FAILED, task->src, 0, "out of memory");
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  }
  for(size_t i = 0; i < count; i++) {
    stream->paths[i] = strdup(paths[i]);
    if(!stream->paths[i]) {
      stream->path_count = i;
      tar_close(stream);
      task_update(task, TASK_FAILED, task->src, 0, "out of memory");
      return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
    }
  }
  resp = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN,
                                           TRANSFER_HTTP_BUFFER_SIZE,
                                           tar_read, stream, tar_close);
  if(!resp) {
    tar_close(stream);
    return MHD_NO;
  }
  MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                          "application/x-tar");
  download_archive_name(download_name, sizeof(download_name), paths, count);
  add_download_filename_header(resp, download_name);
  ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
  MHD_destroy_response(resp);
  return ret;
}
