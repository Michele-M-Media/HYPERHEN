#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "filemgr_internal.h"
#include "path_util.h"

#define ETA_AVERAGE_WINDOW_SECONDS 30
/* Keep results for the initiating browser despite polling from other clients. */
#define TRANSFER_RESULT_RETENTION_SECONDS (5 * 60)

pthread_mutex_t g_tasks_lock = PTHREAD_MUTEX_INITIALIZER;
file_task_t *g_tasks = NULL;
unsigned long g_next_task_id = 1;

const char *
task_op_name(task_op_t op) {
  switch(op) {
  case TASK_COPY: return "copy";
  case TASK_MOVE: return "move";
  case TASK_DELETE: return "delete";
  case TASK_CHMOD: return "chmod";
  case TASK_DOWNLOAD: return "download";
  case TASK_UPLOAD: return "upload";
  case TASK_EXTRACT: return "extract";
  case TASK_PKG_INSTALL: return "pkg_install";
  default: return "unknown";
  }
}

const char *
task_state_name(task_state_t state) {
  switch(state) {
  case TASK_QUEUED: return "queued";
  case TASK_RUNNING: return "running";
  case TASK_DONE: return "done";
  case TASK_FAILED: return "failed";
  case TASK_CANCELED: return "canceled";
  default: return "unknown";
  }
}

int
task_is_active(const file_task_t *task) {
  return task->state == TASK_QUEUED || task->state == TASK_RUNNING;
}

int
has_active_task_locked(void) {
  file_task_t *task;

  for(task = g_tasks; task; task = task->next) {
    if(task->op != TASK_PKG_INSTALL && task_is_active(task)) {
      return 1;
    }
  }
  return 0;
}

int
has_active_task(void) {
  int active;

  pthread_mutex_lock(&g_tasks_lock);
  active = has_active_task_locked();
  pthread_mutex_unlock(&g_tasks_lock);
  return active;
}

void
free_task(file_task_t *task) {
  if(!task) {
    return;
  }
  free_paths(task->srcs, task->src_count);
  free_paths(task->extract_destinations, task->src_count);
  if(task->password) {
    memset(task->password, 0, strlen(task->password));
    free(task->password);
  }
  free(task);
}

void
remove_finished_tasks_locked(void) {
  file_task_t **link = &g_tasks;
  time_t now = time(NULL);

  while(*link) {
    file_task_t *task = *link;

    if(task_is_active(task) || task->active_streams ||
       (((task->op == TASK_DOWNLOAD && !task->reported) || task->op == TASK_UPLOAD) &&
        now - task->updated_at < TRANSFER_RESULT_RETENTION_SECONDS) ||
       (task->op == TASK_PKG_INSTALL && !task->reported)) {
      link = &task->next;
      continue;
    }

    *link = task->next;
    free_task(task);
  }
}

int
task_cancel_requested(file_task_t *task) {
  int cancel;

  pthread_mutex_lock(&g_tasks_lock);
  cancel = task->cancel_requested;
  pthread_mutex_unlock(&g_tasks_lock);
  if(cancel) {
    errno = ECANCELED;
  }
  return cancel;
}

file_task_t *
find_task_locked(unsigned long id) {
  file_task_t *task;

  for(task = g_tasks; task; task = task->next) {
    if(task->id == id) {
      return task;
    }
  }
  return NULL;
}

static long long
timespec_delta_ns(const struct timespec *end, const struct timespec *start) {
  return (long long)(end->tv_sec - start->tv_sec) * 1000000000LL +
         (long long)(end->tv_nsec - start->tv_nsec);
}

static void
task_update_eta_locked(file_task_t *task, const struct timespec *now_mono) {
  task_eta_sample_t *sample;
  task_eta_sample_t *base = NULL;
  unsigned int i;

  if(!task->total || !task->done || task->done >= task->total) {
    task->eta = 0;
    return;
  }

  sample = &task->eta_samples[task->eta_sample_next];
  sample->done = task->done;
  sample->time = *now_mono;
  task->eta_sample_next = (task->eta_sample_next + 1) % ETA_SAMPLE_SLOTS;
  if(task->eta_sample_count < ETA_SAMPLE_SLOTS) {
    task->eta_sample_count++;
  }

  for(i = 0; i < task->eta_sample_count; i++) {
    task_eta_sample_t *candidate = &task->eta_samples[i];
    long long age_ns;

    if(!candidate->time.tv_sec || candidate->done >= task->done) {
      continue;
    }
    age_ns = timespec_delta_ns(now_mono, &candidate->time);
    if(age_ns <= 0 || age_ns > (long long)ETA_AVERAGE_WINDOW_SECONDS * 1000000000LL) {
      continue;
    }
    if(!base || age_ns > timespec_delta_ns(now_mono, &base->time)) {
      base = candidate;
    }
  }

  if(base) {
    long long elapsed_ns = timespec_delta_ns(now_mono, &base->time);
    unsigned long long delta = task->done - base->done;
    unsigned long long remaining = task->total - task->done;
    if(delta && elapsed_ns > 0) {
      long double seconds = (long double)elapsed_ns / 1000000000.0L;
      long double eta = ((long double)remaining / (long double)delta) * seconds;
      task->eta = eta > 0 ? (unsigned long long)(eta + 0.999999L) : 0;
      return;
    }
  }

  task->eta = task->speed ? (task->total - task->done + task->speed - 1) / task->speed : 0;
}

void
task_update(file_task_t *task, task_state_t state, const char *current,
            unsigned long long add_done, const char *error) {
  struct timespec now_mono;
  time_t now;

  clock_gettime(CLOCK_MONOTONIC, &now_mono);
  now = time(NULL);

  pthread_mutex_lock(&g_tasks_lock);
  task->state = state;
  if(current) {
    snprintf(task->current, sizeof(task->current), "%s", current);
  }
  if(add_done) {
    if(!task->transfer_started_at) {
      task->transfer_started_at = now;
    }
    task->done += add_done;
    if(task->total && task->done > task->total) {
      task->done = task->total;
    }
    if(task->speed_sample_time.tv_sec) {
      long long elapsed_ns = timespec_delta_ns(&now_mono, &task->speed_sample_time);
      if(elapsed_ns >= 250000000LL) {
        unsigned long long delta = task->done - task->speed_sample_done;
        task->speed = (unsigned long long)((delta * 1000000000ULL) /
                                           (unsigned long long)elapsed_ns);
        task->speed_sample_done = task->done;
        task->speed_sample_time = now_mono;
      }
    } else {
      task->speed_sample_done = task->done;
      task->speed_sample_time = now_mono;
    }
    task_update_eta_locked(task, &now_mono);
  }
  if(error) {
    snprintf(task->error, sizeof(task->error), "%s", error);
  }
  task->updated_at = now;
  pthread_mutex_unlock(&g_tasks_lock);
}
