#include "filemgr.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
#include "transfer.h"

#define UPLOAD_BUFFER_SIZE (1024 * 1024)

typedef struct upload_context {
  file_task_t *task;
  int fd;
  char *buffer;
  size_t buffered;
  char *writer_buffer;
  size_t writer_size;
  int writer_started;
  int writer_pending;
  int writer_busy;
  int writer_stop;
  int writer_error;
  pthread_t writer_thread;
  pthread_mutex_t writer_mutex;
  pthread_cond_t writer_cond;
  char temp[PATH_MAX];
  char target[PATH_MAX];
  unsigned long long expected;
  unsigned long long written;
  int failed;
  int error;
  int task_done;
  const char *stage;
  char error_message[256];
} upload_context_t;

static const char *
upload_error_message(upload_context_t *ctx) {
  if(!ctx->error_message[0]) {
    snprintf(ctx->error_message, sizeof(ctx->error_message), "%s failed%s%s: %s",
             ctx->stage ? ctx->stage : "upload",
             ctx->target[0] ? " for " : "",
             ctx->target[0] ? ctx->target : "",
             strerror(ctx->error ? ctx->error : EIO));
  }
  return ctx->error_message;
}

static int
upload_write(upload_context_t *ctx, const char *data, size_t size) {
  return transfer_write_all(ctx->task, ctx->fd, ctx->target, data, size,
                            &ctx->written) ? errno : 0;
}

static void *
upload_writer(void *arg) {
  upload_context_t *ctx = arg;

  pthread_mutex_lock(&ctx->writer_mutex);
  for(;;) {
    int error;
    while(!ctx->writer_pending && !ctx->writer_stop) {
      pthread_cond_wait(&ctx->writer_cond, &ctx->writer_mutex);
    }
    if(ctx->writer_stop && !ctx->writer_pending) break;
    ctx->writer_pending = 0;
    ctx->writer_busy = 1;
    pthread_mutex_unlock(&ctx->writer_mutex);
    error = upload_write(ctx, ctx->writer_buffer, ctx->writer_size);
    pthread_mutex_lock(&ctx->writer_mutex);
    if(error) ctx->writer_error = error;
    ctx->writer_busy = 0;
    pthread_cond_broadcast(&ctx->writer_cond);
  }
  pthread_mutex_unlock(&ctx->writer_mutex);
  return NULL;
}

static void
upload_writer_start(upload_context_t *ctx) {
  ctx->writer_buffer = malloc(UPLOAD_BUFFER_SIZE);
  if(!ctx->writer_buffer) return;
  if(pthread_mutex_init(&ctx->writer_mutex, NULL) == 0) {
    if(pthread_cond_init(&ctx->writer_cond, NULL) == 0) {
      ctx->writer_started =
          pthread_create(&ctx->writer_thread, NULL, upload_writer, ctx) == 0;
      if(!ctx->writer_started) pthread_cond_destroy(&ctx->writer_cond);
    }
    if(!ctx->writer_started) pthread_mutex_destroy(&ctx->writer_mutex);
  }
  if(!ctx->writer_started) {
    free(ctx->writer_buffer);
    ctx->writer_buffer = NULL;
  }
}

static int
upload_writer_stop(upload_context_t *ctx, int drain) {
  int error = 0;
  if(!ctx->writer_started) return 0;
  pthread_mutex_lock(&ctx->writer_mutex);
  if(!drain) ctx->writer_pending = 0;
  while(ctx->writer_busy || (drain && ctx->writer_pending)) {
    pthread_cond_wait(&ctx->writer_cond, &ctx->writer_mutex);
  }
  error = ctx->writer_error;
  ctx->writer_stop = 1;
  pthread_cond_signal(&ctx->writer_cond);
  pthread_mutex_unlock(&ctx->writer_mutex);
  pthread_join(ctx->writer_thread, NULL);
  pthread_cond_destroy(&ctx->writer_cond);
  pthread_mutex_destroy(&ctx->writer_mutex);
  ctx->writer_started = 0;
  return error;
}

static int
upload_flush(upload_context_t *ctx) {
  int error;
  if(!ctx->buffered) return 0;
  if(ctx->writer_started) {
    pthread_mutex_lock(&ctx->writer_mutex);
    while(ctx->writer_pending || ctx->writer_busy) {
      pthread_cond_wait(&ctx->writer_cond, &ctx->writer_mutex);
    }
    error = ctx->writer_error;
    if(!error) {
      char *next = ctx->writer_buffer;
      ctx->writer_buffer = ctx->buffer;
      ctx->buffer = next;
      ctx->writer_size = ctx->buffered;
      ctx->buffered = 0;
      ctx->writer_pending = 1;
      pthread_cond_signal(&ctx->writer_cond);
    }
    pthread_mutex_unlock(&ctx->writer_mutex);
  } else {
    error = upload_write(ctx, ctx->buffer, ctx->buffered);
    if(!error) ctx->buffered = 0;
  }
  if(error) {
    ctx->failed = 1;
    ctx->error = error;
    return -1;
  }
  return 0;
}

/* Caller holds g_tasks_lock, including when the last file completes. */
static void
finish_upload_task_locked(file_task_t *task, task_state_t state, const char *current,
                          const char *error) {
  if(!task) {
    return;
  }
  if(task_is_active(task)) {
    time_t completed_at = time(NULL);
    task->state = state;
    if(current) {
      snprintf(task->current, sizeof(task->current), "%s", current);
    }
    if(state == TASK_DONE && task->total) {
      task->done = task->total;
    }
    if(state == TASK_DONE) {
      record_task_completion_locked(task, completed_at);
    }
    if(error) {
      snprintf(task->error, sizeof(task->error), "%s", error);
    }
    if(state == TASK_FAILED) {
      snprintf(task->error_code, sizeof(task->error_code), "upload_failed");
      snprintf(task->error_arg, sizeof(task->error_arg), "%s",
               current ? current : task->src);
    }
    task->updated_at = completed_at;
  }
}

static void
finish_upload_context_error(upload_context_t *ctx) {
  /* No worker may update task progress after a terminal error is published. */
  upload_writer_stop(ctx, 0);
  if(!ctx->task || ctx->task_done) {
    return;
  }
  upload_error_message(ctx);
  pthread_mutex_lock(&g_tasks_lock);
  finish_upload_task_locked(ctx->task,
                     ctx->error == ECANCELED ? TASK_CANCELED : TASK_FAILED,
                     ctx->target[0] ? ctx->target : ctx->task->current,
                     ctx->error == ECANCELED ? "canceled" : ctx->error_message);
  pthread_mutex_unlock(&g_tasks_lock);
  ctx->task_done = 1;
}

static file_task_t *
upload_task_from_conn_locked(struct MHD_Connection *conn) {
  char *idstr = request_value(conn, "X-WFM-Task-ID", "task_id");
  unsigned long id = idstr ? strtoul(idstr, NULL, 10) : 0;
  file_task_t *task = NULL;

  free(idstr);
  if(!id) {
    return NULL;
  }
  task = find_task_locked(id);
  if(task && task->op != TASK_UPLOAD) {
    task = NULL;
  }
  return task;
}

static int
check_upload_manifest_space(const char *base, const char *rels,
                            const char *sizes, unsigned long long fallback_total,
                            char *error, size_t error_size,
                            char *code, size_t code_size,
                            char *arg, size_t arg_size) {
  char *rels_copy = NULL;
  char *sizes_copy = NULL;
  char *rel;
  char *size_text;
  char *rel_save;
  char *size_save;
  unsigned long long available;
  int ret = -1;

  if(!rels || !sizes) {
    return check_target_space(base, fallback_total, error, error_size,
                              code, code_size, arg, arg_size);
  }
  if(target_available_space(base, &available)) {
    snprintf(error, error_size, "cannot read target free space");
    snprintf(code, code_size, "space_check_failed");
    snprintf(arg, arg_size, "%s", base);
    return -1;
  }
  if(!(rels_copy = strdup(rels)) || !(sizes_copy = strdup(sizes))) {
    errno = ENOMEM;
    goto done;
  }

  rel = strtok_r(rels_copy, "\n", &rel_save);
  size_text = strtok_r(sizes_copy, "\n", &size_save);
  while(rel || size_text) {
    char target[PATH_MAX];
    unsigned long long size;

    if(!rel || !size_text || path_join_relative(target, sizeof(target), base, rel)) {
      snprintf(error, error_size, "invalid path");
      snprintf(code, code_size, "invalid_path");
      snprintf(arg, arg_size, "%s", rel ? rel : "");
      errno = EINVAL;
      goto done;
    }

    size = strtoull(size_text, NULL, 10);
    if(available < size) {
      snprintf(error, error_size,
               "not enough target space, required %llu bytes, available %llu bytes",
               size, available);
      snprintf(code, code_size, "no_space");
      snprintf(arg, arg_size, "%llu,%llu", size, available);
      errno = ENOSPC;
      goto done;
    }

    available -= size;

    rel = strtok_r(NULL, "\n", &rel_save);
    size_text = strtok_r(NULL, "\n", &size_save);
  }

  ret = 0;

done:
  free(rels_copy);
  free(sizes_copy);
  return ret;
}

enum MHD_Result
api_upload_prepare(struct MHD_Connection *conn, const char *body,
                   size_t body_size) {
  char *path = fs_path_value(body_form_value(body, body_size, "path"));
  char *src = body_form_value(body, body_size, "src");
  char *total_text = body_form_value(body, body_size, "total");
  char *count_text = body_form_value(body, body_size, "count");
  char *rels = body_form_value(body, body_size, "rels");
  char *sizes = body_form_value(body, body_size, "sizes");
  char *overwrite = body_form_value(body, body_size, "overwrite");
  file_task_t *task = calloc(1, sizeof(*task));
  strbuf_t b = {0};
  unsigned long long total = total_text ? strtoull(total_text, NULL, 10) : 0;
  size_t count = count_text ? (size_t)strtoull(count_text, NULL, 10) : 0;
  char error[128] = {0};
  char code[64] = {0};
  char arg[PATH_MAX + 96] = {0};

  if(!task) {
    free(path); free(src); free(total_text); free(count_text);
    free(rels); free(sizes); free(overwrite);
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  }
  if(!path || !src || !count) {
    free_task(task);
    free(path); free(src); free(total_text); free(count_text);
    free(rels); free(sizes); free(overwrite);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }

  pthread_mutex_lock(&g_tasks_lock);
  remove_finished_tasks_locked();
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    free_task(task);
    free(path); free(src); free(total_text); free(count_text);
    free(rels); free(sizes); free(overwrite);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  pthread_mutex_unlock(&g_tasks_lock);

  if(check_upload_manifest_space(path, rels, sizes, total, error, sizeof(error),
                                 code, sizeof(code), arg, sizeof(arg))) {
    free_task(task);
    free(path); free(src); free(total_text); free(count_text);
    free(rels); free(sizes); free(overwrite);
    return send_json_error_detail(conn,
                                  errno == ENOSPC ? MHD_HTTP_INSUFFICIENT_STORAGE :
                                  MHD_HTTP_INTERNAL_SERVER_ERROR,
                                  error[0] ? error : NULL,
                                  code[0] ? code : NULL,
                                  arg[0] ? arg : NULL);
  }

  task->op = TASK_UPLOAD;
  task->state = TASK_RUNNING;
  task->src_count = count;
  task->file_count = count;
  task->total = total;
  snprintf(task->src, sizeof(task->src), "%s", src);
  snprintf(task->dst, sizeof(task->dst), "%s", path);
  snprintf(task->current, sizeof(task->current), "%s", src);
  task->created_at = time(NULL);
  task->updated_at = task->created_at;

  pthread_mutex_lock(&g_tasks_lock);
  remove_finished_tasks_locked();
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    free_task(task);
    free(path); free(src); free(total_text); free(count_text);
    free(rels); free(sizes); free(overwrite);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  task->id = g_next_task_id++;
  task->next = g_tasks;
  g_tasks = task;
  pthread_mutex_unlock(&g_tasks_lock);

  free(path); free(src); free(total_text); free(count_text);
  free(rels); free(sizes); free(overwrite);
  strbuf_printf(&b, "{\"ok\":true,\"task_id\":%lu}", task->id);
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}

enum MHD_Result
api_upload_finish(struct MHD_Connection *conn) {
  pthread_mutex_lock(&g_tasks_lock);
  file_task_t *task = upload_task_from_conn_locked(conn);
  if(task && task_is_active(task) && task->cancel_requested) {
    finish_upload_task_locked(task, TASK_CANCELED,
                             task->current[0] ? task->current : task->src, "canceled");
  }
  unsigned int status = !task ? MHD_HTTP_NOT_FOUND :
                        task->state == TASK_DONE ? MHD_HTTP_OK : MHD_HTTP_CONFLICT;
  const char *error = !task ? "upload task not found" :
                      task->state == TASK_CANCELED ? "canceled" :
                      task->state == TASK_FAILED ? "upload failed" : "upload is not complete";
  pthread_mutex_unlock(&g_tasks_lock);
  return status == MHD_HTTP_OK ? send_json_ok(conn) : send_json_error(conn, status, error);
}

int
filemgr_upload_begin(struct MHD_Connection *conn, void **upload_ctx) {
  upload_context_t *ctx;
  char *base = fs_path_value(request_value(conn, "X-WFM-Path", "path"));
  char *rel = request_value(conn, "X-WFM-Rel", "rel");
  char *overwrite = request_value(conn, "X-WFM-Overwrite", "overwrite");
  char *size_text = request_value(conn, "X-WFM-Size", "size");
  file_task_t *task;
  char **checked_dirs = NULL;
  size_t checked_dir_count = 0;
  struct stat st;
  char error[128] = {0};
  char code[64] = {0};
  char arg[PATH_MAX + 96] = {0};
  int n;

  *upload_ctx = NULL;
  if(!(ctx = calloc(1, sizeof(*ctx)))) {
    free(base); free(rel); free(overwrite); free(size_text);
    errno = ENOMEM;
    return -1;
  }
  ctx->fd = -1;
  ctx->stage = "preparing upload";
  *upload_ctx = ctx;

  if(size_text) {
    ctx->expected = strtoull(size_text, NULL, 10);
  }
  pthread_mutex_lock(&g_tasks_lock);
  task = upload_task_from_conn_locked(conn);
  if(task && !task_is_active(task)) task = NULL;
  if(task) task->active_streams++;
  ctx->task = task;
  pthread_mutex_unlock(&g_tasks_lock);
  if(task && task_cancel_requested(task)) {
    ctx->failed = 1;
    ctx->error = ECANCELED;
    goto fail;
  }
  if(!task && has_active_task()) {
    ctx->failed = 1;
    ctx->error = EBUSY;
    goto fail;
  }
  ctx->stage = "validating target path";
  if(!base || !rel || path_join_relative(ctx->target, sizeof(ctx->target),
                                         base, rel)) {
    ctx->failed = 1;
    ctx->error = errno ? errno : EINVAL;
    goto fail;
  }
  ctx->stage = "creating target folders";
  if(ensure_parent_dirs(base, rel)) {
    ctx->failed = 1;
    ctx->error = errno ? errno : EACCES;
    goto fail;
  }
  ctx->stage = "checking target path";
  if(!lstat(ctx->target, &st)) {
    if(S_ISDIR(st.st_mode) || !overwrite || strcmp(overwrite, "1")) {
      ctx->failed = 1;
      ctx->error = S_ISDIR(st.st_mode) ? EISDIR : EEXIST;
      goto fail;
    }
  } else if(errno != ENOENT) {
    ctx->failed = 1;
    ctx->error = errno;
    goto fail;
  }
  ctx->stage = "checking target permissions";
  if(check_target_writable(ctx->target, &checked_dirs, &checked_dir_count,
                           error, sizeof(error), code, sizeof(code),
                           arg, sizeof(arg))) {
    snprintf(ctx->error_message, sizeof(ctx->error_message), "%s",
             error[0] ? error : "target is not writable");
    ctx->failed = 1;
    ctx->error = errno ? errno : EACCES;
    goto fail;
  }
  ctx->stage = "checking target space";
  if(check_target_space(ctx->target, ctx->expected, error, sizeof(error),
                        code, sizeof(code), arg, sizeof(arg))) {
    snprintf(ctx->error_message, sizeof(ctx->error_message), "%s",
             error[0] ? error : "not enough target space");
    ctx->failed = 1;
    ctx->error = errno ? errno : ENOSPC;
    goto fail;
  }
  ctx->stage = "creating temporary path";
  n = snprintf(ctx->temp, sizeof(ctx->temp), "%s.wfm-upload-%ld-%lld.tmp",
               ctx->target, (long)getpid(), (long long)time(NULL));
  if(n < 0 || (size_t)n >= sizeof(ctx->temp)) {
    ctx->failed = 1;
    ctx->error = ENAMETOOLONG;
    goto fail;
  }
  ctx->stage = "opening temporary file";
  ctx->fd = open(ctx->temp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if(ctx->fd < 0) {
    ctx->failed = 1;
    ctx->error = errno;
    goto fail;
  }
  ctx->stage = "allocating upload buffer";
  wfm_set_transfer_size(ctx->fd, ctx->expected);
  if(!(ctx->buffer = malloc(UPLOAD_BUFFER_SIZE))) {
    ctx->failed = 1;
    ctx->error = ENOMEM;
    goto fail;
  }
  /* Two bounded buffers overlap reception and writes; fall back on init failure. */
  if(transfer_use_pipeline(ctx->expected)) upload_writer_start(ctx);

fail:
  free_paths(checked_dirs, checked_dir_count);
  free(base); free(rel); free(overwrite); free(size_text);
  if(ctx->failed) {
    finish_upload_context_error(ctx);
    if(ctx->fd >= 0) {
      close(ctx->fd);
      ctx->fd = -1;
    }
    if(ctx->temp[0]) {
      unlink(ctx->temp);
    }
    errno = ctx->error ? ctx->error : EIO;
    return -1;
  }
  ctx->stage = "writing file";
  return 0;
}

int
filemgr_upload_data(void *upload_ctx, const char *data, size_t size) {
  upload_context_t *ctx = upload_ctx;

  if(!ctx || ctx->failed) {
    return -1;
  }
  if(ctx->task && task_cancel_requested(ctx->task)) {
    ctx->failed = 1;
    ctx->error = ECANCELED;
    finish_upload_context_error(ctx);
    return -1;
  }
  while(size) {
    size_t space = UPLOAD_BUFFER_SIZE - ctx->buffered;
    size_t take = size < space ? size : space;

    memcpy(ctx->buffer + ctx->buffered, data, take);
    ctx->buffered += take;
    data += take;
    size -= take;
    if(ctx->buffered == UPLOAD_BUFFER_SIZE && upload_flush(ctx)) {
      finish_upload_context_error(ctx);
      return -1;
    }
  }
  return 0;
}

enum MHD_Result
filemgr_upload_finish(struct MHD_Connection *conn, void *upload_ctx) {
  upload_context_t *ctx = upload_ctx;
  int ret = -1;

  if(!ctx) {
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  if(ctx->failed) {
    finish_upload_context_error(ctx);
    errno = ctx->error ? ctx->error : EIO;
    return send_json_error_detail(conn,
                                  errno == EEXIST ? MHD_HTTP_CONFLICT :
                                  errno == EBUSY ? MHD_HTTP_CONFLICT :
                                  errno == ECANCELED ? MHD_HTTP_CONFLICT :
                                  errno == ENOSPC ? MHD_HTTP_INSUFFICIENT_STORAGE :
                                  MHD_HTTP_INTERNAL_SERVER_ERROR,
                                  ctx->error == ECANCELED ? "canceled" : ctx->error_message,
                                  ctx->error == ECANCELED ? NULL : "upload_failed",
                                  ctx->target[0] ? ctx->target : NULL);
  }
  ctx->stage = "writing file";
  if(ctx->buffered && upload_flush(ctx)) {
    ctx->error = ctx->error ? ctx->error : EIO;
    goto done;
  }
  ctx->error = upload_writer_stop(ctx, 1);
  if(ctx->error) {
    ctx->failed = 1;
    goto done;
  }
  if(ctx->task && task_cancel_requested(ctx->task)) {
    ctx->failed = 1;
    ctx->error = ECANCELED;
    goto done;
  }
  ctx->stage = "verifying uploaded size";
  if(ctx->expected && ctx->written != ctx->expected) {
    ctx->error = EIO;
    goto done;
  }
  ctx->stage = "setting file permissions";
  if(fchmod_0777(ctx->fd)) {
    ctx->error = errno;
    goto done;
  }
  ctx->stage = "syncing file data";
  if(fsync(ctx->fd)) {
    ctx->error = errno;
    goto done;
  }
  ctx->stage = "closing temporary file";
  if(close(ctx->fd)) {
    ctx->fd = -1;
    ctx->error = errno;
    goto done;
  }
  ctx->fd = -1;
  ctx->stage = "moving temporary file into place";
  if(rename(ctx->temp, ctx->target)) {
    ctx->error = errno;
    goto done;
  }
  if(ctx->task) {
    pthread_mutex_lock(&g_tasks_lock);
    ctx->task->upload_completed++;
    if(ctx->task->upload_completed == ctx->task->src_count && !ctx->task->cancel_requested) {
      finish_upload_task_locked(ctx->task, TASK_DONE, ctx->target, NULL);
    }
    pthread_mutex_unlock(&g_tasks_lock);
  }
  ret = 0;

done:
  upload_writer_stop(ctx, 0);
  if(ctx->fd >= 0) {
    close(ctx->fd);
    ctx->fd = -1;
  }
  if(ret) {
    upload_error_message(ctx);
    if(ctx->temp[0]) {
      unlink(ctx->temp);
    }
    finish_upload_context_error(ctx);
    errno = ctx->error ? ctx->error : EIO;
    return send_json_error_detail(conn,
                                  errno == ECANCELED ? MHD_HTTP_CONFLICT :
                                  MHD_HTTP_INTERNAL_SERVER_ERROR,
                                  ctx->error == ECANCELED ? "canceled" : ctx->error_message,
                                  ctx->error == ECANCELED ? NULL : "upload_failed",
                                  ctx->target[0] ? ctx->target : NULL);
  }
  return send_json_ok(conn);
}

void
filemgr_upload_free(void *upload_ctx) {
  upload_context_t *ctx = upload_ctx;

  if(!ctx) {
    return;
  }
  upload_writer_stop(ctx, 0);
  if(ctx->fd >= 0) {
    close(ctx->fd);
    if(ctx->temp[0]) {
      unlink(ctx->temp);
    }
    if(ctx->task && !ctx->task_done) {
      int canceled = task_cancel_requested(ctx->task);
      pthread_mutex_lock(&g_tasks_lock);
      finish_upload_task_locked(ctx->task, canceled ? TASK_CANCELED : TASK_FAILED,
                         ctx->target[0] ? ctx->target : ctx->task->current,
                         canceled ? "canceled" : "client disconnected");
      pthread_mutex_unlock(&g_tasks_lock);
      ctx->task_done = 1;
    }
  }
  free(ctx->buffer);
  free(ctx->writer_buffer);
  if(ctx->task) {
    pthread_mutex_lock(&g_tasks_lock);
    if(ctx->task->active_streams) {
      ctx->task->active_streams--;
    }
    pthread_mutex_unlock(&g_tasks_lock);
  }
  free(ctx);
}
