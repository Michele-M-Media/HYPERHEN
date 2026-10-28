#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "third_party/cJSON.h"
#include "ps5sdk_compat.h"
#include "cr_log.h"
#include "cr_paths.h"
#include "cr_notifications.h"

pthread_mutex_t      g_notifications_lock = PTHREAD_MUTEX_INITIALIZER;
notification_entry_t g_notifications[MAX_NOTIFICATIONS];
int                  g_notifications_count = 0;
int                  g_notification_next_id = 1;

static void *
notify_send_thread(void *arg) {
  notify_request_t *req = arg;
  sceKernelSendNotificationRequest(0, req, sizeof(*req), 0);
  free(req);
  return NULL;
}

/* Fire-and-forget on a detached thread: sceKernelSendNotificationRequest can stall under rapid calls,
 * and notify() is often called while holding g_cheat_apply_lock. */
void
notify(const char *fmt, ...) {
  notify_request_t *req = malloc(sizeof(*req));
  if (!req) return;
  va_list args;
  bzero(req, sizeof(*req));
  va_start(args, fmt);
  vsnprintf(req->message, sizeof(req->message), fmt, args);
  va_end(args);

  pthread_t t;
  pthread_attr_t a;
  pthread_attr_init(&a);
  pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&t, &a, notify_send_thread, req) != 0) {
    free(req);
  }
  pthread_attr_destroy(&a);
}

void
notifications_save(void) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return;
  cJSON *arr = cJSON_AddArrayToObject(root, "notifications");
  pthread_mutex_lock(&g_notifications_lock);
  cJSON_AddNumberToObject(root, "nextId", g_notification_next_id);
  for (int i = 0; i < g_notifications_count; i++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "id", g_notifications[i].id);
    cJSON_AddNumberToObject(e, "ts", (double)g_notifications[i].ts);
    cJSON_AddBoolToObject(e, "read", g_notifications[i].read ? 1 : 0);
    cJSON_AddStringToObject(e, "kind", g_notifications[i].kind);
    cJSON_AddStringToObject(e, "message", g_notifications[i].message);
    cJSON_AddItemToArray(arr, e);
  }
  pthread_mutex_unlock(&g_notifications_lock);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) {
    return;
  }
  write_file_atomic(CHEATRUNNER_NOTIFICATIONS_PATH, (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
notifications_load(void) {
  char *txt = NULL;
  if (read_file_text(CHEATRUNNER_NOTIFICATIONS_PATH, &txt) != 0 || !txt) {
    return;
  }
  cJSON *root = cJSON_Parse(txt);
  free(txt);
  if (!root) {
    return;
  }
  cJSON *arr = cJSON_GetObjectItem(root, "notifications");
  cJSON *next = cJSON_GetObjectItem(root, "nextId");
  pthread_mutex_lock(&g_notifications_lock);
  g_notifications_count = 0;
  if (cJSON_IsNumber(next)) {
    g_notification_next_id = next->valueint > 1 ? next->valueint : 1;
  }
  if (cJSON_IsArray(arr)) {
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_notifications_count < MAX_NOTIFICATIONS; i++) {
      cJSON *e = cJSON_GetArrayItem(arr, i);
      cJSON *id = cJSON_GetObjectItem(e, "id");
      cJSON *ts = cJSON_GetObjectItem(e, "ts");
      cJSON *rd = cJSON_GetObjectItem(e, "read");
      cJSON *k = cJSON_GetObjectItem(e, "kind");
      cJSON *m = cJSON_GetObjectItem(e, "message");
      if (!cJSON_IsNumber(id) || !cJSON_IsString(k) || !cJSON_IsString(m)) {
        continue;
      }
      notification_entry_t *d = &g_notifications[g_notifications_count++];
      d->id = id->valueint;
      d->ts = cJSON_IsNumber(ts) ? (time_t)ts->valuedouble : time(NULL);
      d->read = cJSON_IsBool(rd) ? cJSON_IsTrue(rd) : 0;
      snprintf(d->kind, sizeof(d->kind), "%s", k->valuestring);
      snprintf(d->message, sizeof(d->message), "%s", m->valuestring);
    }
  }
  pthread_mutex_unlock(&g_notifications_lock);
  cJSON_Delete(root);
}

void
notification_add(const char *kind, const char *fmt, ...) {
  char msg[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  pthread_mutex_lock(&g_notifications_lock);
  if (g_notifications_count == MAX_NOTIFICATIONS) {
    memmove(&g_notifications[0], &g_notifications[1], sizeof(g_notifications[0]) * (MAX_NOTIFICATIONS - 1));
    g_notifications_count--;
  }
  notification_entry_t *e = &g_notifications[g_notifications_count++];
  e->id = g_notification_next_id++;
  e->ts = time(NULL);
  e->read = 0;
  snprintf(e->kind, sizeof(e->kind), "%s", kind ? kind : "info");
  snprintf(e->message, sizeof(e->message), "%s", msg);
  pthread_mutex_unlock(&g_notifications_lock);
  notifications_save();
}
