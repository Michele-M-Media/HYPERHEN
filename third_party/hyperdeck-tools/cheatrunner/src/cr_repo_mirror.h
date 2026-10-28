#ifndef CR_REPO_MIRROR_H
#define CR_REPO_MIRROR_H

#include <pthread.h>
#include <time.h>

#define REPO_MIRROR_MISSING_MAX      20
#define REPO_MIRROR_MISSING_NAME_MAX 96

typedef enum {
  REPO_MIRROR_IDLE    = 0,
  REPO_MIRROR_RUNNING = 1,
  REPO_MIRROR_DONE    = 2,
  REPO_MIRROR_ERROR   = 3,
} repo_mirror_state_t;

typedef struct {
  pthread_mutex_t     lock;
  repo_mirror_state_t state;
  char  source[32];
  int   total;
  int   downloaded;
  int   skipped;
  int   failed;
  int   verified;
  int   unchanged;     /* already identical to the repository, not downloaded */
  unsigned long long bytes;  /* body bytes downloaded this run */
  unsigned long long dl_start_ms;
  unsigned long long phase_start_ms;
  unsigned long long archive_ms;  /* time spent downloading archives */
  char  phase[16];     /* listing / archive / extract / downloading / done */
  int   missing_count;
  char  missing[REPO_MIRROR_MISSING_MAX][REPO_MIRROR_MISSING_NAME_MAX];
  char  current[128];
  char  error[256];
  char  mode[32];      /* "git_tree" or "contents_fallback" */
  char  warning[64];   /* e.g. "git_tree_truncated" */
  int   truncated;
  int   complete;
  int   source_idx;    /* current source index when source=all */
  int   source_total;  /* total sources when source=all */
  time_t started_at;
  time_t finished_at;
} repo_mirror_progress_t;

extern repo_mirror_progress_t g_repo_mirror;

/* Starts a background download for 'source' (a REPOS id or "all"); overwrite=0 skips existing files.
 * Returns 0 if started, -1 if already running or the source is invalid. */
int repo_mirror_start(const char *source, int overwrite);

/* Serialise current progress to buf as a JSON object. */
void repo_mirror_status_json(char *buf, size_t buf_size);

#endif /* CR_REPO_MIRROR_H */
