#ifndef CR_ACTIVITY_H
#define CR_ACTIVITY_H

#include <pthread.h>
#include <time.h>

typedef struct activity_title_entry {
  char title_id[16];
  unsigned int launch_count;
  time_t last_launch;
  unsigned int total_seconds;
  char last_cheat[128];
} activity_title_entry_t;

extern pthread_mutex_t g_activity_lock;
extern unsigned int g_activity_launch_count;
extern time_t g_activity_last_launch;
extern char g_activity_last_played_title_id[16];
extern char g_activity_last_cheat_used[128];
extern activity_title_entry_t g_activity_titles[512];
extern int g_activity_titles_count;
extern int g_activity_session_open;
extern char g_activity_session_title_id[16];
extern time_t g_activity_session_start;

int activity_find_title_index_locked(const char *title_id);
void activity_save(void);
void activity_load(void);

#endif /* CR_ACTIVITY_H */
