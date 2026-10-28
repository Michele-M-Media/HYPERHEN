#include "filemgr.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef __linux__
#include <ps5/kernel.h>
#endif

#include "filemgr_internal.h"
#include "archive_extract.h"
#include "archive_helper.h"
#include "json_util.h"
#include "path_util.h"
#include "pkg_info.h"
#include "pkg_installer.h"
#include "websrv.h"
#include "smb.h"
#include "vfs.h"
#include "transfer.h"

#define TRANSFER_ALERT_THRESHOLD (10 * 60)

#ifndef __linux__
typedef struct shell_ui_uri_param {
  uint32_t size;
  uint32_t user_id;
} shell_ui_uri_param_t;

int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t,
                             void *, int *);
int sceUserServiceInitialize(const int *);
int sceUserServiceGetForegroundUser(int *);

static int
navigate_to_home(void) {
  int (*initialize)(void);
  int (*launch_by_uri)(const char *, shell_ui_uri_param_t *);
  shell_ui_uri_param_t param = {.size = sizeof(param)};
  const char *module_path =
    "/system_ex/common_ex/lib/libSceShellUIUtil.sprx";
  const char *uri = "pshomeui:navigateToHome?bootCondition=psButton";
  const int priority = 256;
  int module;
  int result;

  (void)sceUserServiceInitialize(&priority);
  module = sceKernelLoadStartModule(module_path, 0, NULL, 0, NULL, NULL);
  if(module < 0) {
#if WFM_DEBUG
    printf("load libSceShellUIUtil: 0x%08X\n", (unsigned int)module);
#endif
    return module;
  }
  initialize = (void *)kernel_dynlib_dlsym(
    -1, (uint32_t)module, "sceShellUIUtilInitialize");
  launch_by_uri = (void *)kernel_dynlib_dlsym(
    -1, (uint32_t)module, "sceShellUIUtilLaunchByUri");
  if(!initialize || !launch_by_uri) {
#if WFM_DEBUG
    printf("resolve libSceShellUIUtil URI functions failed\n");
#endif
    return -1;
  }

  result = initialize();
  if(result < 0) {
#if WFM_DEBUG
    printf("sceShellUIUtilInitialize: 0x%08X\n", (unsigned int)result);
#endif
  }
  result = sceUserServiceGetForegroundUser((int *)&param.user_id);
  if(result < 0) {
#if WFM_DEBUG
    printf("sceUserServiceGetForegroundUser: 0x%08X\n",
           (unsigned int)result);
#endif
  }
  result = launch_by_uri(uri, &param);
#if WFM_DEBUG
  printf("sceShellUIUtilLaunchByUri: 0x%08X\n", (unsigned int)result);
#endif
  return result;
}
#endif

typedef struct task_completion {
  unsigned long id;
  task_op_t op;
  char src[PATH_MAX];
  size_t src_count;
  unsigned long long total;
  size_t file_count;
  time_t elapsed;
} task_completion_t;

static int ensure_copy_dir(const char *path);
static int open_copy_temp(const char *dst, char *temp, size_t temp_size);

static task_completion_t g_last_completion;
#ifndef __linux__
static pthread_cond_t g_pkg_tasks_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_pkg_worker_thread;
static int g_pkg_worker_started;
#endif

void
record_task_completion_locked(file_task_t *task, time_t completed_at) {
  if((task->op == TASK_COPY || task->op == TASK_MOVE ||
      task->op == TASK_UPLOAD || task->op == TASK_EXTRACT) &&
     completed_at - task->created_at >= TRANSFER_ALERT_THRESHOLD) {
    g_last_completion.id = task->id;
    g_last_completion.op = task->op;
    snprintf(g_last_completion.src, sizeof(g_last_completion.src), "%s", task->src);
    g_last_completion.src_count = task->src_count;
    g_last_completion.total = task->total;
    g_last_completion.file_count = task->dir_count ? task->file_count :
                                   task->op == TASK_UPLOAD ? task->file_count : 0;
    g_last_completion.elapsed = completed_at - task->created_at;
  }
}

int
ensure_parent_dirs(const char *base, const char *rel) {
  char current[PATH_MAX];
  const char *p = rel;
  size_t base_len;

  if(!relative_path_safe(rel)) {
    return -1;
  }
  if(strlen(base) >= sizeof(current)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  strcpy(current, base);
  base_len = strlen(current);
  while(*p) {
    const char *slash = strchr(p, '/');
    size_t len;

    if(!slash) {
      return 0;
    }
    len = (size_t)(slash - rel);
    if(base_len + (strcmp(base, "/") ? 1 : 0) + len >= sizeof(current)) {
      errno = ENAMETOOLONG;
      return -1;
    }
    snprintf(current, sizeof(current), "%s%s%.*s", base,
             strcmp(base, "/") ? "/" : "", (int)len, rel);
    if(ensure_copy_dir(current)) {
      return -1;
    }
    p = slash + 1;
  }
  return 0;
}

static void
task_set_error_code(file_task_t *task, const char *code, const char *arg) {
  pthread_mutex_lock(&g_tasks_lock);
  if(code) {
    snprintf(task->error_code, sizeof(task->error_code), "%s", code);
  }
  if(arg) {
    snprintf(task->error_arg, sizeof(task->error_arg), "%s", arg);
  }
  pthread_mutex_unlock(&g_tasks_lock);
}

static void
task_set_total(file_task_t *task, unsigned long long total) {
  pthread_mutex_lock(&g_tasks_lock);
  task->total = total;
  task->updated_at = time(NULL);
  pthread_mutex_unlock(&g_tasks_lock);
}

static void
task_set_cleanup_stage(file_task_t *task, const char *current) {
  pthread_mutex_lock(&g_tasks_lock);
  task->state = TASK_RUNNING;
  if(current) {
    snprintf(task->current, sizeof(task->current), "%s", current);
  }
  task->speed = 0;
  task->eta = 0;
  task->updated_at = time(NULL);
  pthread_mutex_unlock(&g_tasks_lock);
}

enum MHD_Result
send_buffer(struct MHD_Connection *conn, unsigned int status, char *data,
            const char *mime) {
  struct MHD_Response *resp;
  enum MHD_Result ret = MHD_NO;
  size_t len = data ? strlen(data) : 0;

  if((resp = MHD_create_response_from_buffer(len, data ? data : "",
                                             data ? MHD_RESPMEM_MUST_FREE :
                                                    MHD_RESPMEM_PERSISTENT))) {
    if(mime) {
      MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, mime);
    }
    ret = websrv_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
  } else {
    free(data);
  }

  return ret;
}

enum MHD_Result
send_json_ok(struct MHD_Connection *conn) {
  return send_buffer(conn, MHD_HTTP_OK, strdup("{\"ok\":true}"),
                     "application/json");
}

static const char *
api_error_code(const char *msg) {
  if(!msg) return "system_error";
  if(!strcmp(msg, "another task is running")) return "active_task";
  if(!strcmp(msg, "active task not found")) return "active_task_not_found";
  if(!strcmp(msg, "source and destination are the same")) return "source_destination_same";
  if(!strcmp(msg, "destination is inside source directory")) return "destination_inside_source";
  if(!strcmp(msg, "invalid path")) return "invalid_path";
  if(!strcmp(msg, "file not found")) return "file_not_found";
  if(!strcmp(msg, "invalid method")) return "invalid_method";
  if(!strcmp(msg, "unknown api")) return "unknown_api";
  if(!strcmp(msg, "out of memory")) return "out_of_memory";
  if(!strcmp(msg, "no source paths")) return "no_source_paths";
  if(!strcmp(msg, "file type is not editable")) return "text_type_not_editable";
  if(!strcmp(msg, "text file is too large")) return "text_file_too_large";
  if(!strcmp(msg, "file is not valid UTF-8")) return "text_invalid_utf8";
  if(!strcmp(msg, "file changed since it was opened")) return "text_file_changed";
  if(!strcmp(msg, "text file is not writable")) return "text_file_not_writable";
  if(!strcmp(msg, "file already exists")) return "file_already_exists";
  if(!strcmp(msg, "destination must be a directory for multiple items")) {
    return "destination_must_be_directory";
  }
  return "system_error";
}

enum MHD_Result
send_json_error(struct MHD_Connection *conn, unsigned int status,
                const char *msg) {
  return send_json_error_detail(conn, status, msg, api_error_code(msg), NULL);
}

enum MHD_Result
send_json_error_detail(struct MHD_Connection *conn, unsigned int status,
                       const char *msg, const char *code, const char *arg) {
  strbuf_t b = {0};
  const char *fallback = msg ? msg : strerror(errno);

  strbuf_append(&b, "{\"ok\":false,\"error\":");
  json_escape(&b, fallback);
  strbuf_append(&b, ",\"error_code\":");
  json_escape(&b, code ? code : api_error_code(msg));
  strbuf_append(&b, ",\"error_arg\":");
  json_escape(&b, arg ? arg : fallback);
  strbuf_append(&b, "}");
  return send_buffer(conn, status, b.data, "application/json");
}

static int task_target_path(file_task_t *task, const char *src,
                            char *out, size_t size);
static int chmod_task_path(file_task_t *task, const char *path,
                           unsigned int mode, int recursive);

static int count_path_bytes_sync(file_task_t *task, const char *path,
                                 const char *display,
                                 unsigned long long *total,
                                 size_t *file_count, size_t *dir_count);

static int
count_dir_bytes_sync(file_task_t *task, const char *path, const char *display,
                     unsigned long long *total, size_t *file_count,
                     size_t *dir_count) {
  DIR *dir = opendir(path);
  struct dirent *entry;
  int ret = -1;

  if(!dir) {
    return -1;
  }
  while((entry = readdir(dir))) {
    char child[PATH_MAX];
    char display_child[PATH_MAX];
    if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    if(task && task_cancel_requested(task)) {
      goto done;
    }
    if(path_join(child, sizeof(child), path, entry->d_name) ||
       (display && path_join(display_child, sizeof(display_child), display,
                             entry->d_name)) ||
       count_path_bytes_sync(task, child, display ? display_child : NULL,
                             total, file_count, dir_count)) {
      goto done;
    }
  }
  if(wfm_dir_error(dir)) goto done;
  ret = 0;
done:
  closedir(dir);
  return ret;
}

static int
count_path_bytes_sync(file_task_t *task, const char *path, const char *display,
                      unsigned long long *total, size_t *file_count,
                      size_t *dir_count) {
  struct stat st;

  if(task && task_cancel_requested(task)) {
    return -1;
  }
  if(task) {
    task_update(task, TASK_RUNNING, display ? display : path, 0, NULL);
  }
  if(lstat(path, &st)) {
    return -1;
  }
  if(S_ISDIR(st.st_mode)) {
    if(dir_count) (*dir_count)++;
    return count_dir_bytes_sync(task, path, display, total, file_count,
                                dir_count);
  }
  if(S_ISREG(st.st_mode)) {
    *total += (unsigned long long)st.st_size;
    if(file_count) (*file_count)++;
  }
  return 0;
}

#define count_path_bytes count_path_bytes_sync

int
count_task_path_bytes(file_task_t *task, const char *path, const char *display,
                      unsigned long long *total, size_t *file_count,
                      size_t *dir_count) {
  return count_path_bytes(task, path, display, total, file_count, dir_count);
}

static int
open_copy_temp(const char *dst, char *temp, size_t temp_size) {
  char parent[PATH_MAX];
  char name[96];
  int attempt;

  if(path_dirname(dst, parent, sizeof(parent))) {
    return -1;
  }
  for(attempt = 0; attempt < 32; attempt++) {
    int fd;

    snprintf(name, sizeof(name), ".wfm-copy-%ld-%lld-%d.tmp",
             (long)getpid(), (long long)time(NULL), attempt);
    if(path_join(temp, temp_size, parent, name)) {
      return -1;
    }
    fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if(fd >= 0) {
      return fd;
    }
    if(errno != EEXIST) {
      return -1;
    }
  }
  errno = EEXIST;
  return -1;
}

static int
copy_file(file_task_t *task, const char *src, const char *dst) {
  char *buf = NULL;
  transfer_reader_t *reader = NULL;
  struct stat st;
  size_t buffer_size = TRANSFER_READ_BUFFER_SIZE;
  char temp[PATH_MAX] = {0};
  int in = -1;
  int out = -1;
  int ret = -1;
  ssize_t n;

  task_update(task, TASK_RUNNING, dst, 0, NULL);

  if(task_cancel_requested(task)) {
    errno = ECANCELED;
    return -1;
  }
  if((in = open(src, O_RDONLY)) < 0) {
    goto done;
  }
  if((out = open_copy_temp(dst, temp, sizeof(temp))) < 0) {
    goto done;
  }
  if(fstat(in, &st)) goto done;
  wfm_set_transfer_size(out, (uint64_t)st.st_size);
  reader = transfer_reader_start(in, 0, (uint64_t)st.st_size, task);
  if(!reader) {
    if((uint64_t)st.st_size < buffer_size) buffer_size = st.st_size ? (size_t)st.st_size : 1;
    if(!(buf = malloc(buffer_size))) { errno = ENOMEM; goto done; }
  }
  for(;;) {
    const char *data = buf;
    if(reader) n = transfer_reader_peek(reader, &data);
    else { do { n = read(in, buf, buffer_size); } while(n < 0 && errno == EINTR); }
    if(n <= 0) break;
    if(transfer_write_all(task, out, dst, data, (size_t)n, NULL)) goto done;
    if(reader) transfer_reader_advance(reader, (size_t)n);
  }
  if(n < 0) {
    goto done;
  }
  if(fchmod_0777(out)) {
    goto done;
  }
  if(task_cancel_requested(task)) {
    errno = ECANCELED;
    goto done;
  }
  ret = 0;

done:
  transfer_reader_close(reader);
  free(buf);
  if(in >= 0) close(in);
  if(out >= 0) {
    if(close(out)) ret = -1;
  }
  if(!ret && task_cancel_requested(task)) {
    errno = ECANCELED;
    ret = -1;
  }
  if(!ret && rename(temp, dst)) {
    ret = -1;
  }
  if(ret && temp[0]) {
    unlink(temp);
  }
  return ret;
}

static int copy_path(file_task_t *task, const char *src, const char *dst);
static int remove_path(file_task_t *task, const char *path);
static int check_remove_path_writable(file_task_t *task, const char *path);
static int
ensure_copy_dir(const char *path) {
  struct stat st;

  if(mkdir(path, 0777)) {
    if(errno != EEXIST) {
      return -1;
    }
    if(lstat(path, &st)) {
      return -1;
    }
    if(!S_ISDIR(st.st_mode)) {
      errno = ENOTDIR;
      return -1;
    }
  }
  return chmod_path_0777(path);
}

static int
check_remove_entry_writable(const char *path) {
  char parent[PATH_MAX];

  if(path_dirname(path, parent, sizeof(parent))) {
    return -1;
  }
  return mode_access(parent, W_OK | X_OK);
}

static int
check_remove_dir_writable(file_task_t *task, const char *path) {
  DIR *dir;
  struct dirent *entry;
  int ret = -1;

  if(mode_access(path, R_OK | W_OK | X_OK)) {
    task_update(task, TASK_RUNNING, path, 0, NULL);
    return -1;
  }
  if(!(dir = opendir(path))) {
    task_update(task, TASK_RUNNING, path, 0, NULL);
    return -1;
  }
  while((entry = readdir(dir))) {
    char child[PATH_MAX];

    if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    if(task_cancel_requested(task)) {
      goto done;
    }
    if(path_join(child, sizeof(child), path, entry->d_name) ||
       check_remove_path_writable(task, child)) {
      goto done;
    }
  }
  if(wfm_dir_error(dir)) goto done;
  ret = 0;
done:
  closedir(dir);
  return ret;
}

static int
check_remove_path_writable(file_task_t *task, const char *path) {
  struct stat st;

  if(task_cancel_requested(task)) {
    return -1;
  }
  if(lstat(path, &st)) {
    task_update(task, TASK_RUNNING, path, 0, NULL);
    return -1;
  }
  if(check_remove_entry_writable(path)) {
    task_update(task, TASK_RUNNING, path, 0, NULL);
    return -1;
  }
  if(S_ISDIR(st.st_mode)) {
    return check_remove_dir_writable(task, path);
  }
  return 0;
}

static int
finish_copied_move(file_task_t *task, const char *src) {
  int ret;

  task_set_cleanup_stage(task, "checking source permissions");
  ret = check_remove_path_writable(task, src);
  if(!ret) {
    task_set_cleanup_stage(task, "removing source");
    ret = remove_path(task, src);
  }
  return ret;
}

static int
copy_dir(file_task_t *task, const char *src, const char *dst) {
  DIR *dir;
  struct dirent *entry;
  int ret = -1;

  task_update(task, TASK_RUNNING, dst, 0, NULL);

  if(task_cancel_requested(task)) {
    return -1;
  }
  if(ensure_copy_dir(dst)) {
    return -1;
  }
  if(!(dir = opendir(src))) {
    return -1;
  }

  while((entry = readdir(dir))) {
    char from[PATH_MAX];
    char to[PATH_MAX];

    if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    if(task_cancel_requested(task)) {
      goto done;
    }
    if(path_join(from, sizeof(from), src, entry->d_name) ||
       path_join(to, sizeof(to), dst, entry->d_name) ||
       copy_path(task, from, to)) {
      goto done;
    }
  }

  if(wfm_dir_error(dir)) goto done;
  ret = 0;
done:
  closedir(dir);
  return ret;
}

static int
copy_path(file_task_t *task, const char *src, const char *dst) {
  struct stat st;

  if(lstat(src, &st)) {
    return -1;
  }
  if(S_ISDIR(st.st_mode)) {
    return copy_dir(task, src, dst);
  }
  if(S_ISREG(st.st_mode)) {
    return copy_file(task, src, dst);
  }
  errno = ENOTSUP;
  return -1;
}

static int
move_path(file_task_t *task, const char *src, const char *dst) {
  struct stat src_st;
  struct stat dst_st;
  int dst_exists;
  int ret;

  if(lstat(src, &src_st)) {
    return -1;
  }
  dst_exists = !lstat(dst, &dst_st);
  task_update(task, TASK_RUNNING, dst, 0, NULL);

  if(dst_exists && S_ISDIR(src_st.st_mode) && S_ISDIR(dst_st.st_mode)) {
    ret = copy_path(task, src, dst);
    if(!ret) {
      ret = finish_copied_move(task, src);
    }
    return ret;
  }

  ret = rename(src, dst);
  if(ret && errno == EXDEV) {
    ret = copy_path(task, src, dst);
    if(!ret) {
      ret = finish_copied_move(task, src);
    }
  }
  return ret;
}

static int
remove_path(file_task_t *task, const char *path) {
  struct stat st;

  task_update(task, TASK_RUNNING, path, 0, NULL);

  if(task_cancel_requested(task)) {
    return -1;
  }
  if(lstat(path, &st)) {
    return -1;
  }
  if(S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    if(!dir) {
      return -1;
    }
    while((entry = readdir(dir))) {
      char child[PATH_MAX];
      if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
        continue;
      }
      if(task_cancel_requested(task)) {
        closedir(dir);
        return -1;
      }
      if(path_join(child, sizeof(child), path, entry->d_name) ||
         remove_path(task, child)) {
        closedir(dir);
        return -1;
      }
    }
    if(wfm_dir_error(dir)) {
      closedir(dir);
      return -1;
    }
    closedir(dir);
    return rmdir(path);
  }
  return unlink(path);
}

static int
resolve_destination(const char *src, const char *dst, char *out, size_t size) {
  struct stat st;

  if(!stat(dst, &st) && S_ISDIR(st.st_mode)) {
    return path_join(out, size, dst, path_basename(src));
  }
  if(strlen(dst) >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  strcpy(out, dst);
  return 0;
}

static int
validate_task_target(const char *src, const char *target, int overwrite,
                     char *error, size_t error_size) {
  struct stat src_st;
  struct stat target_st;

  if(!strcmp(src, target)) {
    snprintf(error, error_size, "source and destination are the same");
    errno = EINVAL;
    return -1;
  }
  if(lstat(src, &src_st)) {
    snprintf(error, error_size, "source not found");
    return -1;
  }
  if(S_ISDIR(src_st.st_mode)) {
    size_t src_len = strlen(src);

    while(src_len > 1 && src[src_len - 1] == '/') {
      src_len--;
    }
    if(!strncmp(src, target, src_len) &&
       (target[src_len] == 0 || target[src_len] == '/')) {
      snprintf(error, error_size, "destination is inside source directory");
      errno = EINVAL;
      return -1;
    }
  }
  if(lstat(target, &target_st)) {
    if(errno == ENOENT) {
      return 0;
    }
    snprintf(error, error_size, "cannot check destination");
    return -1;
  }
  if(src_st.st_dev == target_st.st_dev && src_st.st_ino == target_st.st_ino) {
    snprintf(error, error_size, "source and destination are the same");
    errno = EINVAL;
    return -1;
  }
  if(!overwrite) {
    snprintf(error, error_size, "destination exists");
    errno = EEXIST;
    return -1;
  }
  if(S_ISDIR(src_st.st_mode) != S_ISDIR(target_st.st_mode)) {
    snprintf(error, error_size, "remove conflicting file or directory first");
    errno = EEXIST;
    return -1;
  }
  return 0;
}

static int
move_requires_space_check(const char *src, const char *target) {
  struct stat src_st;
  struct stat target_st;
  struct stat dst_dir_st;
  char parent[PATH_MAX];

  if(lstat(src, &src_st)) {
    return -1;
  }
  if(!lstat(target, &target_st)) {
    if(S_ISDIR(src_st.st_mode) && S_ISDIR(target_st.st_mode)) {
      return 1;
    }
    return src_st.st_dev != target_st.st_dev;
  }
  if(path_dirname(target, parent, sizeof(parent)) || stat(parent, &dst_dir_st)) {
    return -1;
  }
  return src_st.st_dev != dst_dir_st.st_dev;
}

static int
check_task_targets_writable(file_task_t *task, char *error, size_t error_size,
                            char *code, size_t code_size, char *arg, size_t arg_size) {
  char **checked_dirs = NULL;
  size_t checked_dir_count = 0;
  size_t i;

  for(i = 0; i < task->src_count; i++) {
    char target[PATH_MAX];

    if(task_cancel_requested(task)) {
      free_paths(checked_dirs, checked_dir_count);
      return -1;
    }
    if(task_target_path(task, task->srcs[i], target, sizeof(target))) {
      snprintf(error, error_size, "target path is too long");
      snprintf(code, code_size, "target_path_too_long");
      free_paths(checked_dirs, checked_dir_count);
      return -1;
    }
    if(check_target_writable(target, &checked_dirs, &checked_dir_count,
                             error, error_size, code, code_size, arg, arg_size)) {
      free_paths(checked_dirs, checked_dir_count);
      return -1;
    }
  }
  free_paths(checked_dirs, checked_dir_count);
  return 0;
}

static int
task_target_path(file_task_t *task, const char *src, char *out, size_t size) {
  if(task->src_count > 1) {
    return path_join(out, size, task->dst, path_basename(src));
  }
  if(strlen(task->dst) >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  strcpy(out, task->dst);
  return 0;
}

static int
request_target_path(const char *src, const char *dst, size_t src_count,
                    char *out, size_t size) {
  if(src_count > 1) {
    return path_join(out, size, dst, path_basename(src));
  }
  return resolve_destination(src, dst, out, size);
}

static enum MHD_Result
task_request_error(struct MHD_Connection *conn, file_task_t *task,
                   char **srcs, size_t src_count,
                   unsigned int status, const char *msg) {
  free_paths(srcs, src_count);
  free(task);
  return send_json_error(conn, status, msg);
}

typedef struct extract_progress_context {
  file_task_t *task;
  unsigned long long last_done;
} extract_progress_context_t;

static int
extract_cancel_requested(void *arg) {
  return task_cancel_requested(((extract_progress_context_t *)arg)->task);
}

static void
extract_progress(void *arg, unsigned long long done,
                 unsigned long long total) {
  extract_progress_context_t *ctx = arg;
  unsigned long long add = done >= ctx->last_done ? done - ctx->last_done : done;

  pthread_mutex_lock(&g_tasks_lock);
  ctx->task->total = total;
  pthread_mutex_unlock(&g_tasks_lock);
  ctx->last_done = done;
  task_update(ctx->task, TASK_RUNNING, NULL, add, NULL);
}

static void
extract_current_file(void *arg, const char *path) {
  extract_progress_context_t *ctx = arg;
  task_update(ctx->task, TASK_RUNNING, path, 0, NULL);
}

static void *
extract_task_worker(file_task_t *task) {
  archive_helper_callbacks_t callbacks;
  archive_helper_result_t helper_result;
  extract_progress_context_t context;
  memset(&context, 0, sizeof(context));
  context.task = task;
  if(task->extract_attached) context.last_done = task->done;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.cancel_requested = extract_cancel_requested;
  callbacks.progress = extract_progress;
  callbacks.current_file = extract_current_file;
  callbacks.arg = &context;
  task_update(task, TASK_RUNNING, task->srcs[0], 0, NULL);

  if((task->extract_attached ?
      archive_helper_attach(task->id, &callbacks, &helper_result) :
      archive_helper_extract(task->id, task->srcs, task->extract_destinations,
                             task->src_count,
                             task->password ? task->password : "",
                             task->extract_overwrite,
                             &callbacks, &helper_result))) {
    if(!strcmp(helper_result.code, "canceled") || task_cancel_requested(task)) {
      task_update(task, TASK_CANCELED,
                  task->current[0] ? task->current : task->src,
                  0, "canceled");
    } else {
      task_set_error_code(task,
                          helper_result.code[0] ? helper_result.code :
                          "archive_extract_failed",
                          !strcmp(helper_result.code, "archive_password_required") ?
                          (task->current[0] ? task->current : task->srcs[0]) :
                          !strcmp(helper_result.code, "archive_missing_volume") ?
                          helper_result.message : NULL);
      task_update(task, TASK_FAILED,
                  task->current[0] ? task->current : task->src, 0,
                  helper_result.message[0] ? helper_result.message :
                  "archive extraction failed");
    }
    return NULL;
  }
  {
    time_t completed_at = time(NULL);
    pthread_mutex_lock(&g_tasks_lock);
    task->upload_completed = task->src_count;
    task->state = TASK_DONE;
    if(task->total) task->done = task->total;
    else if(task->done) task->total = task->done;
    task->updated_at = completed_at;
    record_task_completion_locked(task, completed_at);
    pthread_mutex_unlock(&g_tasks_lock);
  }
  return NULL;
}

static void *
task_worker(void *arg) {
  file_task_t *task = arg;
  unsigned long long total = 0;
  unsigned long long required = 0;
  int ret = -1;

  task_update(task, TASK_RUNNING, "preparing", 0, NULL);

  if(task_cancel_requested(task)) {
    task_update(task, TASK_CANCELED, task->src, 0, "canceled");
    return NULL;
  }
  if(task->op == TASK_EXTRACT) {
    return extract_task_worker(task);
  }
  if(task->op == TASK_COPY || task->op == TASK_MOVE) {
    char error[160] = {0};
    char code[64] = {0};
    char arg[PATH_MAX + 96] = {0};

    task_update(task, TASK_RUNNING, "checking target permissions", 0, NULL);
    if(check_task_targets_writable(task, error, sizeof(error),
                                   code, sizeof(code), arg, sizeof(arg))) {
      if(errno == ECANCELED || task_cancel_requested(task)) {
        task_update(task, TASK_CANCELED, task->current[0] ? task->current : task->src,
                    0, "canceled");
      } else {
        task_set_error_code(task, code, arg);
        task_update(task, TASK_FAILED, task->current[0] ? task->current : task->dst,
                    0, error[0] ? error : strerror(errno));
      }
      return NULL;
    }
  }
  if(task->op == TASK_COPY || task->op == TASK_MOVE) {
    size_t i;
    for(i = 0; i < task->src_count; i++) {
      unsigned long long before = total;
      int needs_space = task->op == TASK_COPY;
      char target[PATH_MAX];

      if(task->op == TASK_COPY || task->op == TASK_MOVE) {
        if(task_target_path(task, task->srcs[i], target, sizeof(target))) {
          task_update(task, TASK_FAILED, task->srcs[i], 0, strerror(errno));
          return NULL;
        }
      }
      if(task->op == TASK_MOVE) {
        needs_space = move_requires_space_check(task->srcs[i], target);
        if(needs_space < 0) {
          task_update(task, TASK_FAILED, task->srcs[i], 0, strerror(errno));
          return NULL;
        }
        if(!needs_space) {
          continue;
        }
      }
      if(count_path_bytes(task, task->srcs[i], task->srcs[i], &total,
                          &task->file_count, &task->dir_count)) {
        if(errno == ECANCELED || task_cancel_requested(task)) {
          task_update(task, TASK_CANCELED, task->srcs[i], 0, "canceled");
        } else {
          if(errno == ENAMETOOLONG) {
            task_set_error_code(task, "path_too_long", NULL);
          }
          task_update(task, TASK_FAILED, task->srcs[i], 0, strerror(errno));
        }
        return NULL;
      }
      if(needs_space) {
        required += total - before;
      }
    }
    task_set_total(task, total);

    if(task->op == TASK_COPY || task->op == TASK_MOVE) {
      char error[128] = {0};
      char code[64] = {0};
      char arg[PATH_MAX] = {0};

      if(check_target_space(task->dst, required, error, sizeof(error),
                            code, sizeof(code), arg, sizeof(arg))) {
        task_set_error_code(task, code, arg);
        task_update(task, TASK_FAILED, task->dst, 0, error[0] ? error : strerror(errno));
        return NULL;
      }
    }
  }

  if(task->op == TASK_COPY) {
    size_t i;
    ret = 0;
    for(i = 0; i < task->src_count && !ret; i++) {
      char target[PATH_MAX];
      if(task_target_path(task, task->srcs[i], target, sizeof(target))) {
        ret = -1;
        break;
      }
      ret = copy_path(task, task->srcs[i], target);
    }
  } else if(task->op == TASK_MOVE) {
    size_t i;
    ret = 0;
    for(i = 0; i < task->src_count && !ret; i++) {
      char target[PATH_MAX];
      if(task_target_path(task, task->srcs[i], target, sizeof(target))) {
        ret = -1;
        break;
      }
      ret = move_path(task, task->srcs[i], target);
    }
  } else if(task->op == TASK_DELETE) {
    size_t i;
    ret = 0;
    for(i = 0; i < task->src_count && !ret; i++) {
      ret = remove_path(task, task->srcs[i]);
    }
  } else if(task->op == TASK_CHMOD) {
    unsigned long long ignored_bytes = 0;
    size_t i;

    ret = 0;
    if(task->recursive) {
      for(i = 0; i < task->src_count; i++) {
        size_t files_before = task->file_count;
        size_t dirs_before = task->dir_count;

        if(count_task_path_bytes(task, task->srcs[i], task->srcs[i],
                                 &ignored_bytes, &task->file_count,
                                 &task->dir_count)) {
          ret = -1;
          break;
        }
        if(files_before == task->file_count && dirs_before == task->dir_count) {
          task->file_count++;
        }
      }
      total = task->file_count + task->dir_count;
    } else {
      total = task->src_count;
    }
    if(!ret) {
      task_set_total(task, total);
      for(i = 0; i < task->src_count && !ret; i++) {
        ret = chmod_task_path(task, task->srcs[i], task->chmod_mode,
                              task->recursive);
      }
    }
  }

  if(ret) {
    if(errno == ECANCELED || task_cancel_requested(task)) {
      task_update(task, TASK_CANCELED, task->current[0] ? task->current : task->src,
                  0, "canceled");
    } else {
      if(task->op == TASK_CHMOD) {
        task_set_error_code(task, "chmod_failed",
                            task->current[0] ? task->current : task->src);
      }
      task_update(task, TASK_FAILED, task->current[0] ? task->current : task->src,
                  0, strerror(errno));
    }
  } else {
    time_t completed_at = time(NULL);
    pthread_mutex_lock(&g_tasks_lock);
    task->state = TASK_DONE;
    if(task->total) {
      task->done = task->total;
    }
    task->updated_at = completed_at;
    record_task_completion_locked(task, completed_at);
    pthread_mutex_unlock(&g_tasks_lock);
  }

  return NULL;
}

void
filemgr_recover_extract_tasks(void) {
  archive_helper_snapshot_t *snapshots = NULL;
  size_t count = 0;
  size_t i;

  if(archive_helper_list_tasks(&snapshots, &count)) return;
  for(i = 0; i < count; i++) {
    archive_helper_snapshot_t *snapshot = &snapshots[i];
    file_task_t *task = calloc(1, sizeof(*task));

    if(!task) continue;
    task->op = TASK_EXTRACT;
    task->state = TASK_QUEUED;
    task->id = snapshot->job_id;
    task->srcs = snapshot->sources;
    task->src_count = snapshot->count;
    task->extract_destinations = snapshot->destinations;
    task->extract_attached = 1;
    task->total = snapshot->total;
    task->done = snapshot->done;
    task->upload_completed = snapshot->completed_count;
    snprintf(task->src, sizeof(task->src), "%s%s", task->srcs[0],
             task->src_count > 1 ? " ..." : "");
    snprintf(task->dst, sizeof(task->dst), "%s", task->extract_destinations[0]);
    snprintf(task->current, sizeof(task->current), "%s", snapshot->current);
    task->created_at = time(NULL);
    task->updated_at = task->created_at;
    snapshot->sources = NULL;
    snapshot->destinations = NULL;
    snapshot->count = 0;

    pthread_mutex_lock(&g_tasks_lock);
    if(task->id >= g_next_task_id) g_next_task_id = task->id + 1;
    task->next = g_tasks;
    g_tasks = task;
    pthread_mutex_unlock(&g_tasks_lock);
    if(pthread_create(&task->thread, NULL, task_worker, task)) {
      task_update(task, TASK_FAILED, NULL, 0, "pthread_create failed");
    } else {
      pthread_detach(task->thread);
    }
  }
  archive_helper_free_snapshots(snapshots, count);
}

static enum MHD_Result
create_task_response(struct MHD_Connection *conn, task_op_t op,
                     char **srcs, size_t src_count, const char *dst,
                     int overwrite, unsigned int chmod_mode, int recursive) {
  file_task_t *task = calloc(1, sizeof(file_task_t));
  strbuf_t b = {0};
  struct stat st;
  size_t i;

  if(!task) {
    free_paths(srcs, src_count);
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "out of memory");
  }
  if(!src_count) {
    return task_request_error(conn, task, srcs, src_count,
                              MHD_HTTP_BAD_REQUEST, "no source paths");
  }
  if(dst && src_count > 1) {
    if(stat(dst, &st) || !S_ISDIR(st.st_mode)) {
      return task_request_error(conn, task, srcs, src_count,
                                MHD_HTTP_BAD_REQUEST,
                                "destination must be a directory for multiple items");
    }
  }
  if(dst) {
    for(i = 0; i < src_count; i++) {
      char target[PATH_MAX];
      char error[128] = {0};
      if(request_target_path(srcs[i], dst, src_count, target, sizeof(target))) {
        return task_request_error(conn, task, srcs, src_count,
                                  MHD_HTTP_BAD_REQUEST, NULL);
      }
      if(validate_task_target(srcs[i], target, overwrite, error, sizeof(error))) {
        return task_request_error(conn, task, srcs, src_count,
                                  MHD_HTTP_CONFLICT, error[0] ? error : NULL);
      }
    }
  }

  task->op = op;
  task->state = TASK_QUEUED;
  task->chmod_mode = chmod_mode;
  task->recursive = recursive;
  task->srcs = srcs;
  task->src_count = src_count;
  snprintf(task->src, sizeof(task->src), "%s%s",
           srcs[0], src_count > 1 ? " ..." : "");
  if(dst) {
    if(src_count == 1) {
      char target[PATH_MAX];
      if(request_target_path(srcs[0], dst, src_count, target, sizeof(target))) {
        return task_request_error(conn, task, srcs, src_count,
                                  MHD_HTTP_BAD_REQUEST, NULL);
      }
      snprintf(task->dst, sizeof(task->dst), "%s", target);
    } else {
      snprintf(task->dst, sizeof(task->dst), "%s", dst);
    }
  }
  task->created_at = time(NULL);
  task->updated_at = task->created_at;

  pthread_mutex_lock(&g_tasks_lock);
  remove_finished_tasks_locked();
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    return task_request_error(conn, task, srcs, src_count,
                              MHD_HTTP_CONFLICT, "another task is running");
  }
  task->id = g_next_task_id++;
  task->next = g_tasks;
  g_tasks = task;
  pthread_mutex_unlock(&g_tasks_lock);

  if(pthread_create(&task->thread, NULL, task_worker, task)) {
    task_update(task, TASK_FAILED, NULL, 0, "pthread_create failed");
  } else {
    pthread_detach(task->thread);
  }

  strbuf_printf(&b, "{\"ok\":true,\"task_id\":%lu}", task->id);
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}

static enum MHD_Result
api_tasks(struct MHD_Connection *conn) {
  strbuf_t b = {0};
  file_task_t *task;
  char *ack_text = query_value(conn, "download_ack");
  unsigned long download_ack = ack_text ? strtoul(ack_text, NULL, 10) : 0;
  time_t now = time(NULL);
  int first = 1;

  free(ack_text);
  strbuf_append(&b, "{\"ok\":true,\"tasks\":[");
  pthread_mutex_lock(&g_tasks_lock);
  for(task = g_tasks; task; task = task->next) {
    if(task->op == TASK_DOWNLOAD && task->id == download_ack && !task_is_active(task))
      task->reported = 1;
    if(!first) {
      strbuf_append(&b, ",");
    }
    first = 0;
    strbuf_printf(&b, "{\"id\":%lu,\"op\":\"%s\",\"state\":\"%s\",",
                  task->id, task_op_name(task->op), task_state_name(task->state));
    strbuf_append(&b, "\"src\":");
    json_escape(&b, task->src);
    strbuf_append(&b, ",\"dst\":");
    json_escape(&b, task->dst);
    strbuf_append(&b, ",\"current\":");
    json_escape(&b, task->current);
    strbuf_append(&b, ",\"error\":");
    json_escape(&b, task->error);
    strbuf_append(&b, ",\"error_code\":");
    json_escape(&b, task->error_code);
    strbuf_append(&b, ",\"error_arg\":");
    json_escape(&b, task->error_arg);
    strbuf_printf(&b, ",\"src_count\":%zu,\"completed_count\":%zu,\"total\":%llu,\"done\":%llu,\"speed\":%llu,\"eta\":%llu,\"cancel_requested\":%s,\"created_at\":%lld,\"elapsed\":%lld,\"total_elapsed\":%lld,\"updated_at\":%lld}",
                  task->src_count, task->upload_completed,
                  task->total, task->done, task->speed, task->eta,
                  task->cancel_requested ? "true" : "false",
                  (long long)task->created_at,
                  task->transfer_started_at ? (long long)(now - task->transfer_started_at) : 0LL,
                  task->created_at ? (long long)(now - task->created_at) : 0LL,
                  (long long)task->updated_at);
    if(!task_is_active(task) && task->op != TASK_DOWNLOAD) task->reported = 1;
  }
  strbuf_printf(&b, "],\"now\":%lld,\"completion\":", (long long)now);
  if(g_last_completion.id) {
    strbuf_printf(&b, "{\"id\":%lu,\"op\":\"%s\",\"src\":",
                  g_last_completion.id, task_op_name(g_last_completion.op));
    json_escape(&b, g_last_completion.src);
    strbuf_printf(&b, ",\"src_count\":%zu,\"elapsed\":%lld,\"total\":%llu,\"file_count\":%zu}",
                  g_last_completion.src_count,
                  (long long)g_last_completion.elapsed,
                  g_last_completion.total, g_last_completion.file_count);
  } else {
    strbuf_append(&b, "null");
  }
  remove_finished_tasks_locked();
  pthread_mutex_unlock(&g_tasks_lock);
  strbuf_append(&b, "}");
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}

static enum MHD_Result
api_cancel(struct MHD_Connection *conn) {
  char *idstr = query_value(conn, "id");
  unsigned long id = idstr ? strtoul(idstr, NULL, 10) : 0;
  file_task_t *task;
  int found = 0;

  free(idstr);
  pthread_mutex_lock(&g_tasks_lock);
  for(task = g_tasks; task; task = task->next) {
    if(task->id == id && task->op != TASK_PKG_INSTALL && task_is_active(task)) {
      task->cancel_requested = 1;
      if(task->op == TASK_DOWNLOAD && task->state == TASK_QUEUED) {
        task->state = TASK_CANCELED;
        snprintf(task->error, sizeof(task->error), "canceled");
      }
      task->updated_at = time(NULL);
      found = 1;
      break;
    }
  }
  pthread_mutex_unlock(&g_tasks_lock);

  return found ? send_json_ok(conn) :
                 send_json_error(conn, MHD_HTTP_NOT_FOUND, "active task not found");
}

static void *
stop_websrv_later(void *arg) {
  (void)arg;
  usleep(250000);
#ifndef __linux__
  (void)navigate_to_home();
  /* Keep this process alive while ShellUI handles the asynchronous URI. */
  usleep(1000000);
#endif
  websrv_stop();
  return NULL;
}

static enum MHD_Result
api_exit(struct MHD_Connection *conn) {
  pthread_t thread;

  if(!pthread_create(&thread, NULL, stop_websrv_later, NULL)) {
    pthread_detach(thread);
  }
  return send_json_ok(conn);
}

static enum MHD_Result
api_copy(struct MHD_Connection *conn, const char *body, size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char *dst = fs_path_value(body_form_value(body, body_size, "dst"));
  char *overwrite = body_form_value(body, body_size, "overwrite");
  char **paths = NULL;
  size_t count = 0;
  enum MHD_Result ret;

  if(!paths_raw || !dst || parse_paths(paths_raw, &paths, &count)) {
    free(paths_raw); free(dst); free(overwrite);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, NULL);
  }
  if(count == 1) {
    char target[PATH_MAX];
    if(request_target_path(paths[0], dst, count, target, sizeof(target)) ||
       !strcmp(paths[0], target)) {
      free_paths(paths, count); free(paths_raw); free(dst); free(overwrite);
      return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "source and destination are the same");
    }
  }
  ret = create_task_response(conn, TASK_COPY, paths, count, dst,
                             overwrite && !strcmp(overwrite, "1"), 0, 0);
  free(paths_raw); free(dst); free(overwrite);
  return ret;
}

static enum MHD_Result
api_move(struct MHD_Connection *conn, const char *body, size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char *dst = fs_path_value(body_form_value(body, body_size, "dst"));
  char *overwrite = body_form_value(body, body_size, "overwrite");
  char **paths = NULL;
  size_t count = 0;
  enum MHD_Result ret;

  if(!paths_raw || !dst || parse_paths(paths_raw, &paths, &count)) {
    free(paths_raw); free(dst); free(overwrite);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, NULL);
  }
  ret = create_task_response(conn, TASK_MOVE, paths, count, dst,
                             overwrite && !strcmp(overwrite, "1"), 0, 0);
  free(paths_raw); free(dst); free(overwrite);
  return ret;
}

static enum MHD_Result
api_delete(struct MHD_Connection *conn, const char *body, size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char **paths = NULL;
  size_t count = 0;
  enum MHD_Result ret;

  if(!paths_raw || parse_paths(paths_raw, &paths, &count)) {
    free(paths_raw);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  for(size_t i = 0; i < count; i++) {
    if(!strcmp(paths[i], "/")) {
      free_paths(paths, count); free(paths_raw);
      return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
    }
  }
  ret = create_task_response(conn, TASK_DELETE, paths, count, NULL, 0, 0, 0);
  free(paths_raw);
  return ret;
}

static enum MHD_Result
api_rename(struct MHD_Connection *conn) {
  char *path = fs_path_value(query_value(conn, "path"));
  char *name = fs_path_value(query_value(conn, "name"));
  char parent[PATH_MAX];
  char target[PATH_MAX];
  int ret;

  pthread_mutex_lock(&g_tasks_lock);
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  pthread_mutex_unlock(&g_tasks_lock);

  if(!path || !name || path_dirname(path, parent, sizeof(parent)) ||
     path_join(target, sizeof(target), parent, name)) {
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, NULL);
  }

  ret = rename(path, target);
  free(path); free(name);
  return ret ? send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, NULL)
             : send_json_ok(conn);
}

static enum MHD_Result
api_mkdir(struct MHD_Connection *conn) {
  char *path = fs_path_value(query_value(conn, "path"));
  char *name = fs_path_value(query_value(conn, "name"));
  char target[PATH_MAX];
  int ret;

  pthread_mutex_lock(&g_tasks_lock);
  if(has_active_task_locked()) {
    pthread_mutex_unlock(&g_tasks_lock);
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  pthread_mutex_unlock(&g_tasks_lock);

  if(!path || !name || path_join(target, sizeof(target), path, name)) {
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, NULL);
  }
  ret = mkdir(target, 0777);
  if(!ret) {
    ret = chmod_path_0777(target);
  }
  free(path); free(name);
  return ret ? send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, NULL)
             : send_json_ok(conn);
}

static int
parse_permission_mode(const char *text, unsigned int *mode) {
  size_t i;

  if(!text || strlen(text) != 4 || text[0] != '0') {
    return -1;
  }
  for(i = 1; i < 4; i++) {
    if(text[i] < '0' || text[i] > '7') {
      return -1;
    }
  }
  *mode = (unsigned int)((text[1] - '0') << 6) |
          (unsigned int)((text[2] - '0') << 3) |
          (unsigned int)(text[3] - '0');
  return 0;
}

static int
chmod_task_path(file_task_t *task, const char *path,
                unsigned int mode, int recursive) {
  struct stat st;

  if(task_cancel_requested(task)) {
    return -1;
  }
  task_update(task, TASK_RUNNING, path, 0, NULL);
  if(lstat(path, &st)) {
    return -1;
  }
  if(recursive && S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    struct dirent *entry;

    if(!dir) {
      return -1;
    }
    while((entry = readdir(dir))) {
      char child[PATH_MAX];
      int error;

      if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
        continue;
      }
      if(task_cancel_requested(task) ||
         path_join(child, sizeof(child), path, entry->d_name) ||
         lstat(child, &st)) {
        error = errno;
        closedir(dir);
        errno = error;
        return -1;
      }
      /* A symlink has no portable mode of its own; never change its target. */
      if(!S_ISLNK(st.st_mode) && chmod_task_path(task, child, mode, 1)) {
        error = errno;
        closedir(dir);
        errno = error;
        return -1;
      }
    }
    if(wfm_dir_error(dir)) {
      closedir(dir);
      return -1;
    }
    closedir(dir);
  }
  task_update(task, TASK_RUNNING, path, 0, NULL);
  if(chmod_path_mode(path, mode)) {
    return -1;
  }
  task_update(task, TASK_RUNNING, path, 1, NULL);
  return 0;
}

static enum MHD_Result
api_launch_elf(struct MHD_Connection *conn, const char *body,
               size_t body_size) {
  char *path = fs_path_value(body_form_value(body, body_size, "path"));
  const char *extension = path ? strrchr(path, '.') : NULL;
  struct stat st;
  enum MHD_Result result;

  if(!path || (path[0] != '/' && !smb_path(path)) ||
     !extension || strcasecmp(extension, ".elf")) {
    free(path);
    return send_json_error_detail(conn, MHD_HTTP_BAD_REQUEST,
                                  "file is not an ELF payload",
                                  "elf_type_invalid", NULL);
  }
  if(lstat(path, &st) || !S_ISREG(st.st_mode)) {
    result = send_json_error_detail(conn, MHD_HTTP_NOT_FOUND,
                                    "ELF payload not found",
                                    "file_not_found", path);
    free(path);
    return result;
  }
  if(has_active_task()) {
    free(path);
    return send_json_error(conn, MHD_HTTP_CONFLICT,
                           "another task is running");
  }

  if(archive_helper_send_elf(path)) {
    int error = errno;
    result = send_json_error_detail(
      conn, error == ENOTSUP ? MHD_HTTP_NOT_IMPLEMENTED :
            error == ENOEXEC ? MHD_HTTP_BAD_REQUEST :
                               MHD_HTTP_INTERNAL_SERVER_ERROR,
      error == ENOTSUP ? "ELF launching is only available on PS5" :
      error == ENOEXEC ? "file is not a valid ELF payload" :
                         "could not send ELF payload to elfldr",
      error == ENOTSUP ? "elf_launch_unsupported" :
      error == ENOEXEC ? "elf_invalid" : "elf_launch_failed", path);
  } else {
    result = send_json_ok(conn);
  }
  free(path);
  return result;
}

static enum MHD_Result
api_extract(struct MHD_Connection *conn, const char *body, size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char *destination = fs_path_value(
    body_form_value(body, body_size, "destination"));
  char *separate_text = body_form_value(body, body_size, "separate");
  char *overwrite_text = body_form_value(body, body_size, "overwrite");
  char *password = body_form_value(body, body_size, "password");
  char **paths = NULL;
  char **destinations = NULL;
  size_t count = 0;
  struct stat st;
  enum MHD_Result result;
  int destination_stat;
  int separate;
  int overwrite;

  if(!paths_raw || !destination ||
     (strcmp(separate_text ? separate_text : "", "0") &&
      strcmp(separate_text ? separate_text : "", "1")) ||
     (strcmp(overwrite_text ? overwrite_text : "0", "0") &&
      strcmp(overwrite_text ? overwrite_text : "", "1")) ||
     parse_paths(paths_raw, &paths, &count)) {
    result = send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
    goto done;
  }
  separate = !strcmp(separate_text, "1");
  overwrite = overwrite_text && !strcmp(overwrite_text, "1");
  if(smb_path(destination)) {
    result = send_json_error(conn, MHD_HTTP_BAD_REQUEST,
                             "archive extraction requires local storage");
    goto done;
  }
  if(destination[0] != '/') {
    result = send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
    goto done;
  }
  destination_stat = lstat(destination, &st);
  if(!destination_stat && !S_ISDIR(st.st_mode)) {
    result = send_json_error_detail(
      conn, MHD_HTTP_CONFLICT, "destination is not a directory",
      "destination_must_be_directory", destination);
    goto done;
  }
  if(destination_stat && errno != ENOENT) {
    result = send_json_error(conn, MHD_HTTP_BAD_REQUEST, NULL);
    goto done;
  }
  if(!(destinations = calloc(count, sizeof(*destinations)))) {
    result = send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                             "out of memory");
    goto done;
  }
  for(size_t i = 0; i < count; i++) {
    char target[PATH_MAX];

    if(smb_path(paths[i])) {
      result = send_json_error(conn, MHD_HTTP_BAD_REQUEST,
                               "copy the archive to local storage first");
      goto done;
    }
    if(lstat(paths[i], &st) || !S_ISREG(st.st_mode)) {
      result = send_json_error_detail(
        conn, MHD_HTTP_NOT_FOUND, "archive file not found",
        "file_not_found", paths[i]);
      goto done;
    }
    if(!archive_path_supported(paths[i])) {
      result = send_json_error_detail(
        conn, MHD_HTTP_BAD_REQUEST, "unsupported archive type",
        "archive_type_unsupported", paths[i]);
      goto done;
    }
    if(archive_output_path(paths[i], destination, separate,
                           target, sizeof(target)) ||
       !(destinations[i] = strdup(target))) {
      result = send_json_error_detail(
        conn, errno == ENOMEM ? MHD_HTTP_INTERNAL_SERVER_ERROR :
                               MHD_HTTP_BAD_REQUEST,
        errno == ENOMEM ? "out of memory" : "target path is too long",
        errno == ENOMEM ? "out_of_memory" : "target_path_too_long",
        errno == ENOMEM ? NULL : destination);
      goto done;
    }
  }
  if(archive_helper_probe()) {
    result = send_json_error_detail(
      conn, MHD_HTTP_SERVICE_UNAVAILABLE,
      "WFM 7zip helper is not running",
      "archive_helper_not_running", NULL);
    goto done;
  }
  {
    file_task_t *task = calloc(1, sizeof(*task));
    strbuf_t response = {0};

    if(!task || (password && password[0] &&
                 !(task->password = strdup(password)))) {
      free_task(task);
      result = send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                               "out of memory");
      goto done;
    }
    task->op = TASK_EXTRACT;
    task->state = TASK_QUEUED;
    task->srcs = paths;
    task->src_count = count;
    task->extract_destinations = destinations;
    task->extract_overwrite = overwrite;
    snprintf(task->src, sizeof(task->src), "%s%s",
             paths[0], count > 1 ? " ..." : "");
    snprintf(task->dst, sizeof(task->dst), "%s", destination);
    task->created_at = time(NULL);
    task->updated_at = task->created_at;
    paths = NULL;
    destinations = NULL;
    count = 0;

    pthread_mutex_lock(&g_tasks_lock);
    remove_finished_tasks_locked();
    if(has_active_task_locked()) {
      pthread_mutex_unlock(&g_tasks_lock);
      free_task(task);
      result = send_json_error(conn, MHD_HTTP_CONFLICT,
                               "another task is running");
      goto done;
    }
    task->id = g_next_task_id++;
    task->next = g_tasks;
    g_tasks = task;
    pthread_mutex_unlock(&g_tasks_lock);

    if(pthread_create(&task->thread, NULL, task_worker, task)) {
      task_update(task, TASK_FAILED, NULL, 0, "pthread_create failed");
    } else {
      pthread_detach(task->thread);
    }
    strbuf_printf(&response, "{\"ok\":true,\"task_id\":%lu}", task->id);
    result = send_buffer(conn, MHD_HTTP_OK, response.data, "application/json");
  }

done:
  if(password) memset(password, 0, strlen(password));
  free(paths_raw);
  free(destination);
  free(separate_text);
  free(overwrite_text);
  free(password);
  free_paths(paths, count);
  free_paths(destinations, count);
  return result;
}

static enum MHD_Result
api_chmod(struct MHD_Connection *conn, const char *body, size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char *mode_text = body_form_value(body, body_size, "mode");
  char *recursive_text = body_form_value(body, body_size, "recursive");
  char **paths = NULL;
  size_t count = 0;
  unsigned int mode;
  enum MHD_Result ret;

  if(!paths_raw || parse_paths(paths_raw, &paths, &count)) {
    free(paths_raw); free(mode_text); free(recursive_text);
    free_paths(paths, count);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  if(parse_permission_mode(mode_text, &mode)) {
    free(paths_raw); free(mode_text); free(recursive_text);
    free_paths(paths, count);
    return send_json_error_detail(conn, MHD_HTTP_BAD_REQUEST,
                                  "invalid permission mode",
                                  "invalid_mode", NULL);
  }
  ret = create_task_response(conn, TASK_CHMOD, paths, count, NULL, 0, mode,
                             recursive_text && !strcmp(recursive_text, "1"));
  free(paths_raw); free(mode_text); free(recursive_text);
  return ret;
}

#ifndef __linux__
static file_task_t *
next_pkg_task_locked(void) {
  file_task_t *task;
  file_task_t *next = NULL;

  for(task = g_tasks; task; task = task->next) {
    if(task->op == TASK_PKG_INSTALL && task->state == TASK_QUEUED &&
       (!next || task->id < next->id)) next = task;
  }
  return next;
}

static void *
pkg_task_worker(void *arg) {
  (void)arg;

  while(1) {
    file_task_t *task;
    int result;

    pthread_mutex_lock(&g_tasks_lock);
    while(!(task = next_pkg_task_locked())) {
      pthread_cond_wait(&g_pkg_tasks_cond, &g_tasks_lock);
    }
    task->state = TASK_RUNNING;
    snprintf(task->current, sizeof(task->current), "%s", task->src);
    task->updated_at = time(NULL);
    pthread_mutex_unlock(&g_tasks_lock);

    result = pkg_installer_install(task->srcs[0]);
    if(result) {
      char error_code[16];
      snprintf(error_code, sizeof(error_code), "0x%08X", (unsigned int)result);
      task_set_error_code(task, "pkg_install_failed", error_code);
      task_update(task, TASK_FAILED, task->src, 0,
                  "package installation failed");
    } else {
      task_update(task, TASK_DONE, task->src, 0, NULL);
    }
  }
  return NULL;
}

static file_task_t *
active_pkg_task_locked(const char *path) {
  file_task_t *task;

  for(task = g_tasks; task; task = task->next) {
    if(task->op == TASK_PKG_INSTALL && task_is_active(task) &&
       !strcmp(task->srcs[0], path)) return task;
  }
  return NULL;
}
#endif

static int
enqueue_pkg_tasks(char **paths, size_t count, unsigned long *ids) {
#ifdef __linux__
  (void)paths; (void)count; (void)ids;
  return PKG_INSTALL_UNSUPPORTED;
#else
  file_task_t **pending = calloc(count, sizeof(*pending));
  int result = 0;

  if(!pending) return -1;
  for(size_t i = 0; i < count; i++) {
    file_task_t *task = calloc(1, sizeof(*task));
    if(!task || !(task->srcs = calloc(1, sizeof(*task->srcs))) ||
       !(task->srcs[0] = strdup(paths[i]))) {
      free_task(task);
      result = -1;
      goto done;
    }
    task->op = TASK_PKG_INSTALL;
    task->state = TASK_QUEUED;
    task->src_count = 1;
    snprintf(task->src, sizeof(task->src), "%s", paths[i]);
    task->created_at = time(NULL);
    task->updated_at = task->created_at;
    pending[i] = task;
  }

  pthread_mutex_lock(&g_tasks_lock);
  if(!g_pkg_worker_started) {
    result = pthread_create(&g_pkg_worker_thread, NULL, pkg_task_worker, NULL);
    if(!result) {
      pthread_detach(g_pkg_worker_thread);
      g_pkg_worker_started = 1;
    }
  }
  if(!result) {
    for(size_t i = 0; i < count; i++) {
      file_task_t *existing = active_pkg_task_locked(paths[i]);
      if(existing) {
        ids[i] = existing->id;
        free_task(pending[i]);
      } else {
        pending[i]->id = g_next_task_id++;
        ids[i] = pending[i]->id;
        pending[i]->next = g_tasks;
        g_tasks = pending[i];
      }
      pending[i] = NULL;
    }
    pthread_cond_signal(&g_pkg_tasks_cond);
  }
  pthread_mutex_unlock(&g_tasks_lock);

done:
  for(size_t i = 0; i < count; i++) free_task(pending[i]);
  free(pending);
  return result;
#endif
}

static enum MHD_Result
api_install_pkg(struct MHD_Connection *conn, const char *body,
                size_t body_size) {
  char *paths_raw = body_form_value(body, body_size, "paths");
  char **paths = NULL;
  unsigned long *ids = NULL;
  size_t count = 0;
  strbuf_t json = {0};
  int result;

  if(!paths_raw || parse_paths(paths_raw, &paths, &count) ||
     !(ids = calloc(count, sizeof(*ids)))) {
    free(paths_raw); free_paths(paths, count); free(ids);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  for(size_t i = 0; i < count; i++) {
    const char *extension = strrchr(paths[i], '.');
    struct stat st;

    if(!extension || strcasecmp(extension, ".pkg")) {
      enum MHD_Result ret = send_json_error_detail(
        conn, MHD_HTTP_BAD_REQUEST, "file is not a PKG package",
        "pkg_type_invalid", paths[i]);
      free(paths_raw); free_paths(paths, count); free(ids);
      return ret;
    }
    if(stat(paths[i], &st) || !S_ISREG(st.st_mode)) {
      free(paths_raw); free_paths(paths, count); free(ids);
      return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
    }
  }

  result = enqueue_pkg_tasks(paths, count, ids);
  free(paths_raw); free_paths(paths, count);
  if(result == PKG_INSTALL_UNSUPPORTED) {
    free(ids);
    return send_json_error_detail(conn, MHD_HTTP_NOT_IMPLEMENTED,
                                  "package installation is only available on PS5",
                                  "pkg_install_unsupported", NULL);
  }
  if(result) {
    free(ids);
    return send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                           "could not queue package installation");
  }

  /* Keep the action from completing instantly after an accidental press. */
  usleep(600000);
  strbuf_append(&json, "{\"ok\":true,\"task_ids\":[");
  for(size_t i = 0; i < count; i++) {
    if(i) strbuf_append(&json, ",");
    strbuf_printf(&json, "%lu", ids[i]);
  }
  strbuf_append(&json, "]}");
  free(ids);
  return send_buffer(conn, MHD_HTTP_OK, json.data, "application/json");
}

enum MHD_Result
filemgr_api_request(struct MHD_Connection *conn, const char *url,
                    const char *method, const char *body, size_t body_size) {
  if(!strcmp(url, "/api/list")) return api_list(conn);
  if(!strcmp(url, "/api/tasks")) return api_tasks(conn);
  if(!strcmp(url, "/api/space")) return api_space(conn);
  if(!strcmp(url, "/api/smb/shares")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_smb_shares(conn, body, body_size);
  }
  if(!strcmp(url, "/api/smb/add") || !strcmp(url, "/api/smb/connect")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_smb_add(conn, body, body_size, !strcmp(url, "/api/smb/connect"));
  }
  if(!strcmp(url, "/api/smb/remove")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_smb_remove(conn, body, body_size);
  }
  if(!strcmp(url, "/api/cancel")) return api_cancel(conn);
  if(!strcmp(url, "/api/exit")) return api_exit(conn);
  if(!strcmp(url, "/api/copy")) return api_copy(conn, body, body_size);
  if(!strcmp(url, "/api/move")) return api_move(conn, body, body_size);
  if(!strcmp(url, "/api/delete")) return api_delete(conn, body, body_size);
  if(!strcmp(url, "/api/download/prepare")) return api_download_prepare(conn, body, body_size);
  if(!strcmp(url, "/api/download")) {
    return strcmp(method, MHD_HTTP_METHOD_GET) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_download(conn);
  }
  if(!strcmp(url, "/api/upload/prepare")) return api_upload_prepare(conn, body, body_size);
  if(!strcmp(url, "/api/upload/finish")) return api_upload_finish(conn);
  if(!strcmp(url, "/api/rename")) return api_rename(conn);
  if(!strcmp(url, "/api/mkdir")) return api_mkdir(conn);
  if(!strcmp(url, "/api/launch-elf")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_launch_elf(conn, body, body_size);
  }
  if(!strcmp(url, "/api/chmod")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_chmod(conn, body, body_size);
  }
  if(!strcmp(url, "/api/extract")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_extract(conn, body, body_size);
  }
  if(!strcmp(url, "/api/pkg-info")) {
    return strcmp(method, MHD_HTTP_METHOD_GET) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_pkg_info(conn);
  }
  if(!strcmp(url, "/api/pkg-icon")) {
    return strcmp(method, MHD_HTTP_METHOD_GET) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_pkg_icon(conn);
  }
  if(!strcmp(url, "/api/install-pkg")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_install_pkg(conn, body, body_size);
  }
  if(!strcmp(url, "/api/text")) {
    return strcmp(method, MHD_HTTP_METHOD_GET) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_text(conn);
  }
  if(!strcmp(url, "/api/text/create")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_text_create(conn);
  }
  if(!strcmp(url, "/api/text/save")) {
    return strcmp(method, MHD_HTTP_METHOD_POST) ?
      send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method") :
      api_text_save(conn, body, body_size);
  }
  return send_json_error(conn, MHD_HTTP_NOT_FOUND, "unknown api");
}
