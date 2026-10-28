#ifndef CR_NOTIFICATIONS_H
#define CR_NOTIFICATIONS_H

#include <pthread.h>
#include <time.h>

#define MAX_NOTIFICATIONS 256

typedef struct notification_entry {
  int    id;
  time_t ts;
  int    read;
  char   kind[32];
  char   message[256];
} notification_entry_t;

extern pthread_mutex_t      g_notifications_lock;
extern notification_entry_t g_notifications[MAX_NOTIFICATIONS];
extern int                  g_notifications_count;
extern int                  g_notification_next_id;

void notify(const char *fmt, ...);
void notifications_save(void);
void notifications_load(void);
void notification_add(const char *kind, const char *fmt, ...);

#endif /* CR_NOTIFICATIONS_H */
