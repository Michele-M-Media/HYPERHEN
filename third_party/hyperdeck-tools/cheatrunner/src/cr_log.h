#ifndef CR_LOG_H
#define CR_LOG_H

#include <pthread.h>
#include <time.h>

#define MAX_LOG_ENTRIES 512

typedef struct log_entry {
  int seq;
  time_t ts;
  char level[8];
  char tag[24];
  char message[256];
} log_entry_t;

extern pthread_mutex_t g_log_lock;
extern log_entry_t     g_logs[MAX_LOG_ENTRIES];
extern int             g_log_start;
extern int             g_log_count;
extern int             g_log_next_seq;

enum {
  CR_LOG_LEVEL_DEBUG = 0,
  CR_LOG_LEVEL_INFO  = 1,
  CR_LOG_LEVEL_WARN  = 2,
  CR_LOG_LEVEL_ERROR = 3,
};

void log_push(const char *level, const char *tag, const char *message);
void cr_log(const char *level, const char *tag, const char *fmt, ...);
void log_msg(const char *fmt, ...);
void cr_log_set_level(const char *level);
int cr_log_level_enabled(const char *level);
void cr_log_klog_banner(void);

#endif /* CR_LOG_H */
