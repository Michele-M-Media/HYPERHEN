#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "cr_log.h"
#include "cr_paths.h"
#include "build_id.h"

/* Writes to klogsrv through sendsyslog (syscall 0x259); the <118> prefix is the syslog priority
 * (kernel, info). */
static void
klog_raw(const char *buf) {
  syscall(0x259, 7, buf, 0);
}

void
cr_log_klog_banner(void) {
  static const char * const lines[] = {
    "<118>[CheatRunner] \n",
    "<118>[CheatRunner]    ____ _                _   ____                              \n",
    "<118>[CheatRunner]   / ___| |__   ___  __ _| |_|  _ \\ _   _ _ __  _ __   ___ _ __ \n",
    "<118>[CheatRunner]  | |   | '_ \\ / _ \\/ _` | __| |_) | | | | '_ \\| '_ \\ / _ \\ '__|\n",
    "<118>[CheatRunner]  | |___| | | |  __/ (_| | |_|  _ <| |_| | | | | | | |  __/ |   \n",
    "<118>[CheatRunner]   \\____|_| |_|\\___|\\__,_|\\__|_| \\_\\\\__,_|_| |_|_| |_|\\___|_|   \n",
    "<118>[CheatRunner] \n",
    NULL
  };
  for (int i = 0; lines[i]; i++) {
    klog_raw(lines[i]);
  }
}

static void
klog_send(const char *level, const char *tag, const char *msg) {
  char buf[640];
  int n = snprintf(buf, sizeof(buf), "<118>[CheatRunner] [%s] [%s] %s\n",
                   level ? level : "info", tag ? tag : "core", msg ? msg : "");
  if (n > 0) {
    klog_raw(buf);
  }
}

/* Plain-text log at CHEATRUNNER_LOG_PATH, appended to across sessions. Each session starts
 * with a header line, and a file over 2 MB is moved to ".previous". Caller must hold g_log_lock. */
#define LOG_FILE_MAX_BYTES (2 * 1024 * 1024)
static FILE *g_log_file = NULL;

static FILE *
log_file_open(void) {
    struct stat st;
    if (stat(CHEATRUNNER_LOG_PATH, &st) == 0 && st.st_size > LOG_FILE_MAX_BYTES) {
        char previous[sizeof(CHEATRUNNER_LOG_PATH) + 16];
        snprintf(previous, sizeof(previous), "%s.previous", CHEATRUNNER_LOG_PATH);
        rename(CHEATRUNNER_LOG_PATH, previous);
    }
    FILE *f = fopen(CHEATRUNNER_LOG_PATH, "a");
    if (f) {
        fprintf(f, "\n--- CheatRunner %s (build %s) session pid=%d ---\n", CHEATRUNNER_VERSION, CHEATRUNNER_BUILD_ID, (int)getpid());
        fflush(f);
    }
    return f;
}

static void
log_file_line(FILE *f, const char *level, const char *tag, const char *message, time_t ts) {
  struct tm tmv;
  char tbuf[24];
  localtime_r(&ts, &tmv);
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
  fprintf(f, "%s [%s] [%s] %s\n", tbuf, level, tag, message);
}

static void
log_file_write_locked(const char *level, const char *tag, const char *message, time_t ts) {
  if (!g_log_file) {
    /* /data/cheatrunner may not exist this early in boot; keep retrying instead of giving up for the session. */
    g_log_file = log_file_open();
    if (g_log_file) {
      /* Lines logged before the file could be opened; the newest entry is the one being written. */
      for (int i = 0; i < g_log_count - 1; i++) {
        const log_entry_t *e = &g_logs[(g_log_start + i) % MAX_LOG_ENTRIES];
        log_file_line(g_log_file, e->level, e->tag, e->message, e->ts);
      }
    }
  }
  if (!g_log_file) {
    return;
  }
  log_file_line(g_log_file, level, tag, message, ts);
  fflush(g_log_file);
}

pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
log_entry_t     g_logs[MAX_LOG_ENTRIES];
int             g_log_start = 0;
int             g_log_count = 0;
int             g_log_next_seq = 0;
static volatile int g_log_min_level = CR_LOG_LEVEL_DEBUG;

static int
log_level_from_string(const char *level) {
  if (!level || !level[0]) {
    return CR_LOG_LEVEL_INFO;
  }
  if (strcasecmp(level, "debug") == 0) {
    return CR_LOG_LEVEL_DEBUG;
  }
  if (strcasecmp(level, "info") == 0) {
    return CR_LOG_LEVEL_INFO;
  }
  if (strcasecmp(level, "warn") == 0 || strcasecmp(level, "warning") == 0) {
    return CR_LOG_LEVEL_WARN;
  }
  if (strcasecmp(level, "error") == 0) {
    return CR_LOG_LEVEL_ERROR;
  }
  return CR_LOG_LEVEL_INFO;
}

void
log_push(const char *level, const char *tag, const char *message) {
  pthread_mutex_lock(&g_log_lock);
  time_t now = time(NULL);
  if (g_log_count > 0) {
    int prev_idx = (g_log_start + g_log_count - 1) % MAX_LOG_ENTRIES;
    if (!strcmp(g_logs[prev_idx].level, level ? level : "info") &&
        !strcmp(g_logs[prev_idx].tag, tag ? tag : "core") &&
        !strcmp(g_logs[prev_idx].message, message ? message : "") &&
        (now - g_logs[prev_idx].ts) <= 5) {
      pthread_mutex_unlock(&g_log_lock);
      return;
    }
  }
  int idx = (g_log_start + g_log_count) % MAX_LOG_ENTRIES;
  if (g_log_count == MAX_LOG_ENTRIES) {
    idx = g_log_start;
    g_log_start = (g_log_start + 1) % MAX_LOG_ENTRIES;
  } else {
    g_log_count++;
  }
  g_logs[idx].seq = g_log_next_seq++;
  g_logs[idx].ts = now;
  snprintf(g_logs[idx].level, sizeof(g_logs[idx].level), "%s", level ? level : "info");
  snprintf(g_logs[idx].tag, sizeof(g_logs[idx].tag), "%s", tag ? tag : "core");
  snprintf(g_logs[idx].message, sizeof(g_logs[idx].message), "%s", message ? message : "");
  log_file_write_locked(g_logs[idx].level, g_logs[idx].tag, g_logs[idx].message, now);
  pthread_mutex_unlock(&g_log_lock);
}

void
cr_log_set_level(const char *level) {
  g_log_min_level = log_level_from_string(level);
}

int
cr_log_level_enabled(const char *level) {
  int msg_level = log_level_from_string(level);
  return msg_level >= g_log_min_level;
}

void
cr_log(const char *level, const char *tag, const char *fmt, ...) {
  if (!cr_log_level_enabled(level)) {
    return;
  }
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  printf("[%s] %s\n", tag ? tag : "core", buf);
  log_push(level, tag, buf);
  klog_send(level, tag, buf);
}

void
log_msg(const char *fmt, ...) {
  if (!cr_log_level_enabled("info")) {
    return;
  }
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  printf("%s\n", buf);
  log_push("info", "core", buf);
  klog_send("info", "core", buf);
}
