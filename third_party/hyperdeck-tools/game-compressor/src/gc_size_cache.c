/*
 * Game Compressor - background folder-size estimate cache.
 */

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "gc_diag.h"
#include "gc_size_cache.h"
#include "transfer_internal.h"

#define GC_BASE "/data/GameCompressor"
#define GC_SIZE_CACHE_LOG GC_BASE "/size-cache.jsonl"
#define GC_SIZE_CACHE_MAX 256
#define GC_SIZE_QUEUE_MAX 128
#define GC_WORKER_THREAD_STACK_SIZE (1024 * 1024)

typedef struct gc_size_cache_entry {
  int used;
  int queued;
  int scanning;
  int scanned_this_run;
  int failed_this_run;
  char path[1024];
  uint64_t size;
  time_t measured_at;
} gc_size_cache_entry_t;

static pthread_mutex_t g_size_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static gc_size_cache_entry_t g_size_cache[GC_SIZE_CACHE_MAX];
static char g_size_queue[GC_SIZE_QUEUE_MAX][1024];
static size_t g_size_queue_count = 0;
static int g_size_cache_loaded = 0;
static int g_size_worker_running = 0;

const char *
gc_size_status_name(gc_size_status_t status) {
  if(status == GC_SIZE_STATUS_CACHED) return "cached";
  if(status == GC_SIZE_STATUS_QUEUED) return "queued";
  if(status == GC_SIZE_STATUS_SCANNING) return "scanning";
  if(status == GC_SIZE_STATUS_REFRESHING) return "refreshing";
  if(status == GC_SIZE_STATUS_DONE) return "done";
  if(status == GC_SIZE_STATUS_FAILED) return "failed";
  return "unknown";
}

static uint64_t
json_find_u64_value(const char *json, const char *key, uint64_t fallback) {
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key ? key : "");
  const char *p = strstr(json ? json : "", pattern);
  if(!p) return fallback;
  p += strlen(pattern);
  while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if(*p != ':') return fallback;
  p++;
  while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  return (uint64_t)strtoull(p, NULL, 10);
}

static int
json_find_string_value(const char *json, const char *key, char *out,
                       size_t out_size) {
  char pattern[128];
  if(!out || out_size == 0) return 0;
  out[0] = 0;
  snprintf(pattern, sizeof(pattern), "\"%s\"", key ? key : "");
  const char *p = strstr(json ? json : "", pattern);
  if(!p) return 0;
  p += strlen(pattern);
  while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if(*p != ':') return 0;
  p++;
  while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if(*p != '"') return 0;
  p++;
  size_t pos = 0;
  while(*p && *p != '"' && pos + 1 < out_size) {
    if(*p == '\\' && p[1]) p++;
    out[pos++] = *p++;
  }
  out[pos] = 0;
  return pos > 0;
}

static int
size_cache_find_locked(const char *path) {
  if(!path || !path[0]) return -1;
  for(size_t i = 0; i < GC_SIZE_CACHE_MAX; i++) {
    if(g_size_cache[i].used && !strcmp(g_size_cache[i].path, path)) {
      return (int)i;
    }
  }
  return -1;
}

static int
size_cache_slot_locked(void) {
  int oldest = -1;
  time_t oldest_time = 0;
  for(size_t i = 0; i < GC_SIZE_CACHE_MAX; i++) {
    if(!g_size_cache[i].used) return (int)i;
    if(g_size_cache[i].queued || g_size_cache[i].scanning) continue;
    if(oldest < 0 || g_size_cache[i].measured_at < oldest_time) {
      oldest = (int)i;
      oldest_time = g_size_cache[i].measured_at;
    }
  }
  return oldest >= 0 ? oldest : 0;
}

static void
size_cache_store_memory_locked(const char *path, uint64_t size,
                               time_t measured_at) {
  if(!path || !path[0]) return;
  int idx = size_cache_find_locked(path);
  if(idx < 0) idx = size_cache_slot_locked();
  memset(&g_size_cache[idx], 0, sizeof(g_size_cache[idx]));
  g_size_cache[idx].used = 1;
  snprintf(g_size_cache[idx].path, sizeof(g_size_cache[idx].path), "%s", path);
  g_size_cache[idx].size = size;
  g_size_cache[idx].measured_at = measured_at > 0 ? measured_at : time(NULL);
}

static void
size_cache_forget_memory_locked(const char *path) {
  int idx = size_cache_find_locked(path);
  if(idx >= 0) memset(&g_size_cache[idx], 0, sizeof(g_size_cache[idx]));
}

static void
size_cache_clear_scan_state_memory_locked(const char *path) {
  int idx = size_cache_find_locked(path);
  if(idx >= 0) {
    g_size_cache[idx].queued = 0;
    g_size_cache[idx].scanning = 0;
  }
}

static void
size_cache_clear_queued_measurements_locked(void) {
  for(size_t i = 0; i < g_size_queue_count; i++) {
    size_cache_clear_scan_state_memory_locked(g_size_queue[i]);
  }
  g_size_queue_count = 0;
}

static void
size_cache_remove_from_queue_locked(const char *path) {
  if(!path || !path[0]) return;
  size_t out = 0;
  for(size_t i = 0; i < g_size_queue_count; i++) {
    if(!strcmp(g_size_queue[i], path)) continue;
    if(out != i) {
      snprintf(g_size_queue[out], sizeof(g_size_queue[out]), "%s",
               g_size_queue[i]);
    }
    out++;
  }
  g_size_queue_count = out;
}

static gc_size_status_t
size_cache_status_locked(int idx) {
  if(idx < 0 || idx >= (int)GC_SIZE_CACHE_MAX ||
     !g_size_cache[idx].used) {
    return GC_SIZE_STATUS_UNKNOWN;
  }
  if(g_size_cache[idx].scanning) {
    return g_size_cache[idx].size > 0
        ? GC_SIZE_STATUS_REFRESHING
        : GC_SIZE_STATUS_SCANNING;
  }
  if(g_size_cache[idx].queued) {
    return g_size_cache[idx].size > 0
        ? GC_SIZE_STATUS_REFRESHING
        : GC_SIZE_STATUS_QUEUED;
  }
  if(g_size_cache[idx].failed_this_run) return GC_SIZE_STATUS_FAILED;
  if(g_size_cache[idx].scanned_this_run) return GC_SIZE_STATUS_DONE;
  if(g_size_cache[idx].size > 0 && g_size_cache[idx].measured_at > 0) {
    return GC_SIZE_STATUS_CACHED;
  }
  return GC_SIZE_STATUS_UNKNOWN;
}

static void
size_cache_load_locked(void) {
  if(g_size_cache_loaded) return;
  g_size_cache_loaded = 1;

  FILE *f = fopen(GC_SIZE_CACHE_LOG, "r");
  if(!f) return;

  char line[2048];
  while(fgets(line, sizeof(line), f)) {
    char path[1024] = {0};
    struct stat st;
    line[strcspn(line, "\r\n")] = 0;
    if(!json_find_string_value(line, "path", path, sizeof(path)) ||
       !path_is_safe(path)) {
      continue;
    }
    uint64_t valid = json_find_u64_value(line, "valid", 1);
    if(!valid) {
      size_cache_forget_memory_locked(path);
      continue;
    }
    if(lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
      size_cache_forget_memory_locked(path);
      continue;
    }
    uint64_t size = json_find_u64_value(line, "size", 0);
    uint64_t measured_at = json_find_u64_value(line, "measuredAt", 0);
    size_cache_store_memory_locked(path, size, (time_t)measured_at);
  }
  fclose(f);
}

static void
size_cache_append_row(const char *path, uint64_t size, int valid,
                      time_t measured_at) {
  if(mkdirs(GC_BASE) != 0) return;
  int fd = open(GC_SIZE_CACHE_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if(fd < 0) return;
  json_buf_t b = {0};
  if(json_append(&b, "{\"path\":") == 0 &&
     json_string(&b, path) == 0 &&
     json_appendf(&b, ",\"size\":%llu,\"valid\":%d,"
                  "\"measuredAt\":%ld,\"quality\":\"estimate\"}\n",
                  (unsigned long long)size,
                  valid ? 1 : 0,
                  (long)measured_at) == 0) {
    write_all_fd(fd, b.data, b.len);
    fsync(fd);
  }
  free(b.data);
  close(fd);
}

int
gc_size_cache_lookup(const char *path, uint64_t *size_out,
                     time_t *measured_at_out,
                     gc_size_status_t *status_out) {
  int ok = 0;
  gc_size_status_t status = GC_SIZE_STATUS_UNKNOWN;
  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  int idx = size_cache_find_locked(path);
  if(idx >= 0) {
    status = size_cache_status_locked(idx);
  }
  if(status_out) *status_out = status;
  if(idx >= 0 && g_size_cache[idx].measured_at > 0) {
    if(size_out) *size_out = g_size_cache[idx].size;
    if(measured_at_out) *measured_at_out = g_size_cache[idx].measured_at;
    ok = 1;
  } else if(measured_at_out) {
    *measured_at_out = 0;
  }
  pthread_mutex_unlock(&g_size_cache_lock);
  return ok ? 0 : -1;
}

void
gc_size_cache_store(const char *path, uint64_t size) {
  if(!path || !path_is_safe(path)) return;
  time_t now = time(NULL);
  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  size_cache_store_memory_locked(path, size, now);
  int idx = size_cache_find_locked(path);
  if(idx >= 0) {
    size_cache_remove_from_queue_locked(path);
    g_size_cache[idx].queued = 0;
    g_size_cache[idx].scanning = 0;
    g_size_cache[idx].scanned_this_run = 1;
    g_size_cache[idx].failed_this_run = 0;
  }
  pthread_mutex_unlock(&g_size_cache_lock);
  size_cache_append_row(path, size, 1, now);
}

static void
size_cache_store_estimate_if_changed(const char *path, uint64_t size) {
  if(!path || !path_is_safe(path)) return;
  time_t now = time(NULL);
  int append_row = 0;
  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  int idx = size_cache_find_locked(path);
  if(idx < 0 || g_size_cache[idx].size != size ||
     g_size_cache[idx].measured_at <= 0) {
    append_row = 1;
  }
  size_cache_store_memory_locked(path, size, now);
  idx = size_cache_find_locked(path);
  if(idx >= 0) {
    size_cache_remove_from_queue_locked(path);
    g_size_cache[idx].queued = 0;
    g_size_cache[idx].scanning = 0;
    g_size_cache[idx].scanned_this_run = 1;
    g_size_cache[idx].failed_this_run = 0;
  }
  pthread_mutex_unlock(&g_size_cache_lock);
  if(append_row) size_cache_append_row(path, size, 1, now);
}

void
gc_size_cache_forget(const char *path) {
  if(!path || !path_is_safe(path)) return;
  time_t now = time(NULL);
  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  size_cache_forget_memory_locked(path);
  pthread_mutex_unlock(&g_size_cache_lock);
  size_cache_append_row(path, 0, 0, now);
}

static void
size_cache_clear_measuring(const char *path) {
  if(!path || !path_is_safe(path)) return;
  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  size_cache_clear_scan_state_memory_locked(path);
  pthread_mutex_unlock(&g_size_cache_lock);
}

static uint64_t
rough_alloc_size_for_stat(const struct stat *st) {
  if(!st || !S_ISREG(st->st_mode)) return 0;
  if(st->st_blocks > 0) return (uint64_t)st->st_blocks * 512ULL;
  return st->st_size > 0 ? (uint64_t)st->st_size : 0;
}

static void
size_estimate_walk_inner(const char *path, du_state_t *du) {
  struct stat st;
  if(!du || du->cancelled) return;
  if(lstat(path, &st) != 0) return;
  du->entries++;
  if(S_ISREG(st.st_mode)) {
    du->files++;
    du->bytes += rough_alloc_size_for_stat(&st);
    return;
  }
  if(!S_ISDIR(st.st_mode)) return;
  du->dirs++;
  DIR *d = opendir(path);
  if(!d) return;
  struct dirent *ent;
  while((ent = readdir(d))) {
    if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
    char child[1024];
    join_path(child, sizeof(child), path, ent->d_name);
    size_estimate_walk_inner(child, du);
  }
  closedir(d);
}

static void
size_estimate_walk(const char *path, du_state_t *du) {
  if(!du) return;
  memset(du, 0, sizeof(*du));
  size_estimate_walk_inner(path, du);
}

static void *
size_cache_worker(void *arg) {
  (void)arg;
  while(1) {
    char path[1024];
    pthread_mutex_lock(&g_size_cache_lock);
    if(g_size_queue_count == 0) {
      g_size_worker_running = 0;
      pthread_mutex_unlock(&g_size_cache_lock);
      return NULL;
    }
    snprintf(path, sizeof(path), "%s", g_size_queue[0]);
    int idx = size_cache_find_locked(path);
    if(g_size_queue_count > 1) {
      memmove(&g_size_queue[0], &g_size_queue[1],
              (g_size_queue_count - 1) * sizeof(g_size_queue[0]));
    }
    g_size_queue_count--;
    if(idx >= 0 && g_size_cache[idx].scanned_this_run) {
      g_size_cache[idx].queued = 0;
      g_size_cache[idx].scanning = 0;
      pthread_mutex_unlock(&g_size_cache_lock);
      continue;
    }
    if(idx >= 0) {
      g_size_cache[idx].queued = 0;
      g_size_cache[idx].scanning = 1;
      g_size_cache[idx].failed_this_run = 0;
    }
    pthread_mutex_unlock(&g_size_cache_lock);

    struct stat st;
    if(lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
      gc_log("size estimate skipped missing folder path=%s", path);
      pthread_mutex_lock(&g_size_cache_lock);
      size_cache_load_locked();
      idx = size_cache_find_locked(path);
      if(idx >= 0) {
        g_size_cache[idx].queued = 0;
        g_size_cache[idx].scanning = 0;
        g_size_cache[idx].scanned_this_run = 1;
        g_size_cache[idx].failed_this_run = 1;
      }
      pthread_mutex_unlock(&g_size_cache_lock);
      continue;
    }

    gc_log("size estimate scanning path=%s", path);
    du_state_t du;
    size_estimate_walk(path, &du);
    if(du.cancelled) {
      gc_log("size estimate cancelled path=%s entries=%llu",
             path, (unsigned long long)du.entries);
      size_cache_clear_measuring(path);
      continue;
    }
    size_cache_store_estimate_if_changed(path, du.bytes);
    gc_log("size estimate done path=%s bytes=%llu entries=%llu",
           path, (unsigned long long)du.bytes,
           (unsigned long long)du.entries);
  }
}

static void
size_cache_start_worker_if_needed(int start_worker) {
  if(!start_worker) return;
  pthread_t t;
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&at, GC_WORKER_THREAD_STACK_SIZE);
  int rc = pthread_create(&t, &at, size_cache_worker, NULL);
  pthread_attr_destroy(&at);
  if(rc != 0) {
    pthread_mutex_lock(&g_size_cache_lock);
    g_size_worker_running = 0;
    size_cache_clear_queued_measurements_locked();
    pthread_mutex_unlock(&g_size_cache_lock);
    gc_log("size estimate worker start failed rc=%d", rc);
  }
}

static void
size_cache_queue_measure_ex(const char *path, int front, int recheck_cached) {
  if(!path || !path_is_safe(path)) return;
  int start_worker = 0;

  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  int idx = size_cache_find_locked(path);
  if(idx >= 0 && g_size_cache[idx].scanned_this_run) {
    pthread_mutex_unlock(&g_size_cache_lock);
    return;
  }
  if(idx >= 0 && g_size_cache[idx].scanning) {
    pthread_mutex_unlock(&g_size_cache_lock);
    return;
  }
  for(size_t i = 0; i < g_size_queue_count; i++) {
    if(!strcmp(g_size_queue[i], path)) {
      if(front && i > 0) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", g_size_queue[i]);
        memmove(&g_size_queue[1], &g_size_queue[0],
                i * sizeof(g_size_queue[0]));
        snprintf(g_size_queue[0], sizeof(g_size_queue[0]), "%s", tmp);
      }
      pthread_mutex_unlock(&g_size_cache_lock);
      return;
    }
  }
  if(!recheck_cached && idx >= 0 && g_size_cache[idx].measured_at > 0 &&
     g_size_cache[idx].size > 0) {
    pthread_mutex_unlock(&g_size_cache_lock);
    return;
  }
  if(g_size_queue_count >= GC_SIZE_QUEUE_MAX) {
    gc_log("size estimate queue full path=%s", path);
    pthread_mutex_unlock(&g_size_cache_lock);
    return;
  }
  int had_entry = idx >= 0;
  if(idx < 0) idx = size_cache_slot_locked();
  uint64_t old_size = had_entry ? g_size_cache[idx].size : 0;
  time_t old_measured_at = had_entry ? g_size_cache[idx].measured_at : 0;
  memset(&g_size_cache[idx], 0, sizeof(g_size_cache[idx]));
  g_size_cache[idx].used = 1;
  g_size_cache[idx].queued = 1;
  g_size_cache[idx].size = old_size;
  g_size_cache[idx].measured_at = old_measured_at;
  snprintf(g_size_cache[idx].path, sizeof(g_size_cache[idx].path), "%s", path);
  if(front && g_size_queue_count > 0) {
    memmove(&g_size_queue[1], &g_size_queue[0],
            g_size_queue_count * sizeof(g_size_queue[0]));
    snprintf(g_size_queue[0], sizeof(g_size_queue[0]), "%s", path);
    g_size_queue_count++;
  } else {
    snprintf(g_size_queue[g_size_queue_count++], sizeof(g_size_queue[0]),
             "%s", path);
  }
  if(!g_size_worker_running) {
    g_size_worker_running = 1;
    start_worker = 1;
  }
  pthread_mutex_unlock(&g_size_cache_lock);

  size_cache_start_worker_if_needed(start_worker);
}

void
gc_size_cache_queue_measure(const char *path) {
  size_cache_queue_measure_ex(path, 0, 0);
}

void
gc_size_cache_queue_recheck(const char *path) {
  size_cache_queue_measure_ex(path, 0, 1);
}

gc_size_status_t
gc_size_cache_priority(const char *path) {
  if(!path || !path_is_safe(path)) return GC_SIZE_STATUS_UNKNOWN;
  int start_worker = 0;
  gc_size_status_t status = GC_SIZE_STATUS_UNKNOWN;
  pthread_mutex_lock(&g_size_cache_lock);
  size_cache_load_locked();
  int idx = size_cache_find_locked(path);
  if(idx >= 0 && g_size_cache[idx].scanning) {
    status = size_cache_status_locked(idx);
    pthread_mutex_unlock(&g_size_cache_lock);
    return status;
  }
  if(idx >= 0 && g_size_cache[idx].scanned_this_run) {
    status = size_cache_status_locked(idx);
    pthread_mutex_unlock(&g_size_cache_lock);
    return status;
  }
  for(size_t i = 0; i < g_size_queue_count; i++) {
    if(!strcmp(g_size_queue[i], path)) {
      if(i > 0) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", g_size_queue[i]);
        memmove(&g_size_queue[1], &g_size_queue[0],
                i * sizeof(g_size_queue[0]));
        snprintf(g_size_queue[0], sizeof(g_size_queue[0]), "%s", tmp);
      }
      idx = size_cache_find_locked(path);
      status = idx >= 0 ? size_cache_status_locked(idx)
                        : GC_SIZE_STATUS_QUEUED;
      pthread_mutex_unlock(&g_size_cache_lock);
      return status;
    }
  }
  if(g_size_queue_count >= GC_SIZE_QUEUE_MAX) {
    gc_log("size estimate priority queue full path=%s", path);
    pthread_mutex_unlock(&g_size_cache_lock);
    return GC_SIZE_STATUS_UNKNOWN;
  }
  int had_entry = idx >= 0;
  if(idx < 0) idx = size_cache_slot_locked();
  uint64_t old_size = had_entry ? g_size_cache[idx].size : 0;
  time_t old_measured_at = had_entry ? g_size_cache[idx].measured_at : 0;
  memset(&g_size_cache[idx], 0, sizeof(g_size_cache[idx]));
  g_size_cache[idx].used = 1;
  g_size_cache[idx].queued = 1;
  g_size_cache[idx].size = old_size;
  g_size_cache[idx].measured_at = old_measured_at;
  snprintf(g_size_cache[idx].path, sizeof(g_size_cache[idx].path), "%s", path);
  if(g_size_queue_count > 0) {
    memmove(&g_size_queue[1], &g_size_queue[0],
            g_size_queue_count * sizeof(g_size_queue[0]));
  }
  snprintf(g_size_queue[0], sizeof(g_size_queue[0]), "%s", path);
  g_size_queue_count++;
  status = size_cache_status_locked(idx);
  if(!g_size_worker_running) {
    g_size_worker_running = 1;
    start_worker = 1;
  }
  pthread_mutex_unlock(&g_size_cache_lock);
  size_cache_start_worker_if_needed(start_worker);
  return status;
}
