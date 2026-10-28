#pragma once

#include <stddef.h>

typedef struct archive_helper_callbacks {
  int (*cancel_requested)(void *arg);
  void (*progress)(void *arg, unsigned long long done,
                   unsigned long long total);
  void (*current_file)(void *arg, const char *path);
  void *arg;
} archive_helper_callbacks_t;

typedef struct archive_helper_result {
  char code[64];
  char message[160];
} archive_helper_result_t;

typedef struct archive_helper_snapshot {
  unsigned long job_id;
  unsigned int state;
  unsigned int current_index;
  unsigned int completed_count;
  unsigned long long total;
  unsigned long long done;
  char *current;
  char *error_code;
  char *error_message;
  char **sources;
  char **destinations;
  size_t count;
} archive_helper_snapshot_t;

#define ARCHIVE_HELPER_TASK_RUNNING 1U
#define ARCHIVE_HELPER_TASK_DONE 2U
#define ARCHIVE_HELPER_TASK_FAILED 3U
#define ARCHIVE_HELPER_TASK_CANCELED 4U

int archive_helper_autostart(void);
int archive_helper_send_elf(const char *path);
int archive_helper_probe(void);
int archive_helper_list_tasks(archive_helper_snapshot_t **snapshots,
                              size_t *count);
void archive_helper_free_snapshots(archive_helper_snapshot_t *snapshots,
                                   size_t count);
int archive_helper_attach(unsigned long job_id,
                          const archive_helper_callbacks_t *callbacks,
                          archive_helper_result_t *result);
int archive_helper_extract(unsigned long job_id, char *const *sources,
                           char *const *destinations, size_t count,
                           const char *password, int overwrite,
                           const archive_helper_callbacks_t *callbacks,
                           archive_helper_result_t *result);
