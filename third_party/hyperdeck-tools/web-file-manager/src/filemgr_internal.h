#pragma once

#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <time.h>

#include <microhttpd.h>

#define ETA_SAMPLE_SLOTS 64

typedef enum task_op {
  TASK_COPY,
  TASK_MOVE,
  TASK_DELETE,
  TASK_CHMOD,
  TASK_DOWNLOAD,
  TASK_UPLOAD,
  TASK_EXTRACT,
  TASK_PKG_INSTALL,
} task_op_t;

typedef enum task_state {
  TASK_QUEUED,
  TASK_RUNNING,
  TASK_DONE,
  TASK_FAILED,
  TASK_CANCELED,
} task_state_t;

typedef struct task_eta_sample {
  unsigned long long done;
  struct timespec time;
} task_eta_sample_t;

typedef struct file_task {
  unsigned long id;
  task_op_t op;
  task_state_t state;
  char src[PATH_MAX];
  char dst[PATH_MAX];
  char current[PATH_MAX];
  char error[160];
  char error_code[64];
  char error_arg[PATH_MAX + 96];
  char **srcs;
  size_t src_count;
  char *password;
  char **extract_destinations;
  int extract_attached;
  size_t file_count;
  size_t dir_count;
  size_t upload_completed;
  unsigned int chmod_mode;
  int recursive;
  int extract_overwrite;
  unsigned long long total;
  unsigned long long done;
  unsigned long long speed;
  unsigned long long eta;
  unsigned long long speed_sample_done;
  struct timespec speed_sample_time;
  task_eta_sample_t eta_samples[ETA_SAMPLE_SLOTS];
  unsigned int eta_sample_next;
  unsigned int eta_sample_count;
  int cancel_requested;
  int reported; /* Terminal state has been included in /api/tasks. */
  unsigned int active_streams;
  time_t created_at;
  time_t transfer_started_at;
  time_t updated_at;
  pthread_t thread;
  struct file_task *next;
} file_task_t;

extern pthread_mutex_t g_tasks_lock;
extern file_task_t *g_tasks;
extern unsigned long g_next_task_id;

const char *task_op_name(task_op_t op);
const char *task_state_name(task_state_t state);
int task_is_active(const file_task_t *task);
int has_active_task_locked(void);
int has_active_task(void);
void free_task(file_task_t *task);
void remove_finished_tasks_locked(void);
int task_cancel_requested(file_task_t *task);
file_task_t *find_task_locked(unsigned long id);
void task_update(file_task_t *task, task_state_t state, const char *current,
                 unsigned long long add_done, const char *error);
void record_task_completion_locked(file_task_t *task, time_t completed_at);

enum MHD_Result send_json_ok(struct MHD_Connection *conn);
enum MHD_Result send_json_error(struct MHD_Connection *conn,
                                unsigned int status, const char *msg);
enum MHD_Result send_json_error_detail(struct MHD_Connection *conn,
                                       unsigned int status, const char *msg,
                                       const char *code, const char *arg);
enum MHD_Result send_buffer(struct MHD_Connection *conn, unsigned int status,
                            char *data, const char *mime);

int ensure_parent_dirs(const char *base, const char *rel);
int chmod_path_mode(const char *path, unsigned int mode);
int chmod_path_0777(const char *path);
int fchmod_0777(int fd);
int ignore_chmod_error(int err);
int mode_access(const char *path, int mode);
int check_target_writable(const char *target, char ***checked_dirs,
                          size_t *checked_dir_count,
                          char *error, size_t error_size,
                          char *code, size_t code_size,
                          char *arg, size_t arg_size);
int check_target_space(const char *target, unsigned long long required,
                       char *error, size_t error_size,
                       char *code, size_t code_size,
                       char *arg, size_t arg_size);
int target_available_space(const char *target, unsigned long long *available);
int count_task_path_bytes(file_task_t *task, const char *path,
                          const char *display, unsigned long long *total,
                          size_t *file_count, size_t *dir_count);

enum MHD_Result api_upload_prepare(struct MHD_Connection *conn,
                                   const char *body, size_t body_size);
enum MHD_Result api_upload_finish(struct MHD_Connection *conn);
enum MHD_Result api_download_prepare(struct MHD_Connection *conn,
                                     const char *body, size_t body_size);
enum MHD_Result api_download(struct MHD_Connection *conn);
enum MHD_Result api_list(struct MHD_Connection *conn);
enum MHD_Result api_space(struct MHD_Connection *conn);
enum MHD_Result api_text(struct MHD_Connection *conn);
enum MHD_Result api_text_create(struct MHD_Connection *conn);
enum MHD_Result api_text_save(struct MHD_Connection *conn, const char *body,
                              size_t body_size);
