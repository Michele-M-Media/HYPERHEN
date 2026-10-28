#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "third_party/cJSON.h"
#include "cr_activity.h"
#include "cr_paths.h"

pthread_mutex_t g_activity_lock = PTHREAD_MUTEX_INITIALIZER;
unsigned int g_activity_launch_count = 0;
time_t g_activity_last_launch = 0;
char g_activity_last_played_title_id[16] = "";
char g_activity_last_cheat_used[128] = "";
activity_title_entry_t g_activity_titles[512];
int g_activity_titles_count = 0;
int g_activity_session_open = 0;
char g_activity_session_title_id[16] = "";
time_t g_activity_session_start = 0;

static int
activity_title_id_valid(const char *title_id) {
  if (!title_id) {
    return 0;
  }
  size_t len = strlen(title_id);
  if (len != 9) {
    return 0;
  }
  if (strncmp(title_id, "CUSA", 4) != 0 && strncmp(title_id, "PPSA", 4) != 0 &&
      strncmp(title_id, "NPXS", 4) != 0 && strncmp(title_id, "PCAS", 4) != 0) {
    return 0;
  }
  for (size_t i = 4; i < len; i++) {
    if (!isalnum((unsigned char)title_id[i])) {
      return 0;
    }
  }
  return 1;
}

int
activity_find_title_index_locked(const char *title_id) {
  for (int i = 0; i < g_activity_titles_count; i++) {
    if (!strcmp(g_activity_titles[i].title_id, title_id)) {
      return i;
    }
  }
  return -1;
}

void
activity_save(void) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return;
  cJSON *arr = cJSON_AddArrayToObject(root, "titles");
  pthread_mutex_lock(&g_activity_lock);
  cJSON_AddNumberToObject(root, "launchCount", g_activity_launch_count);
  cJSON_AddNumberToObject(root, "lastLaunch", (double)g_activity_last_launch);
  cJSON_AddStringToObject(root, "lastPlayedTitleId", g_activity_last_played_title_id);
  cJSON_AddStringToObject(root, "lastCheatUsed", g_activity_last_cheat_used);
  for (int i = 0; i < g_activity_titles_count; i++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "titleId", g_activity_titles[i].title_id);
    cJSON_AddNumberToObject(e, "launchCount", g_activity_titles[i].launch_count);
    cJSON_AddNumberToObject(e, "lastLaunch", (double)g_activity_titles[i].last_launch);
    cJSON_AddNumberToObject(e, "totalSeconds", g_activity_titles[i].total_seconds);
    cJSON_AddStringToObject(e, "lastCheat", g_activity_titles[i].last_cheat);
    cJSON_AddItemToArray(arr, e);
  }
  pthread_mutex_unlock(&g_activity_lock);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) {
    return;
  }
  write_file_atomic(CHEATRUNNER_ACTIVITY_PATH, (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
activity_load(void) {
  char *txt = NULL;
  if (read_file_text(CHEATRUNNER_ACTIVITY_PATH, &txt) != 0 || !txt) {
    return;
  }
  cJSON *root = cJSON_Parse(txt);
  free(txt);
  if (!root) {
    return;
  }
  pthread_mutex_lock(&g_activity_lock);
  cJSON *v = cJSON_GetObjectItem(root, "launchCount");
  if (cJSON_IsNumber(v)) {
    g_activity_launch_count = (unsigned int)v->valueint;
  }
  v = cJSON_GetObjectItem(root, "lastLaunch");
  if (cJSON_IsNumber(v)) {
    g_activity_last_launch = (time_t)v->valuedouble;
  }
  cJSON *s = cJSON_GetObjectItem(root, "lastPlayedTitleId");
  if (cJSON_IsString(s)) {
    snprintf(g_activity_last_played_title_id, sizeof(g_activity_last_played_title_id), "%s", s->valuestring);
  }
  s = cJSON_GetObjectItem(root, "lastCheatUsed");
  if (cJSON_IsString(s)) {
    snprintf(g_activity_last_cheat_used, sizeof(g_activity_last_cheat_used), "%s", s->valuestring);
  }
  cJSON *arr = cJSON_GetObjectItem(root, "titles");
  g_activity_titles_count = 0;
  if (cJSON_IsArray(arr)) {
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_activity_titles_count < (int)(sizeof(g_activity_titles) / sizeof(g_activity_titles[0])); i++) {
      cJSON *e = cJSON_GetArrayItem(arr, i);
      cJSON *id = cJSON_GetObjectItem(e, "titleId");
      if (!cJSON_IsString(id) || !activity_title_id_valid(id->valuestring)) {
        continue;
      }
      activity_title_entry_t *d = &g_activity_titles[g_activity_titles_count++];
      memset(d, 0, sizeof(*d));
      snprintf(d->title_id, sizeof(d->title_id), "%s", id->valuestring);
      cJSON *lc = cJSON_GetObjectItem(e, "launchCount");
      cJSON *ll = cJSON_GetObjectItem(e, "lastLaunch");
      cJSON *ts = cJSON_GetObjectItem(e, "totalSeconds");
      cJSON *lch = cJSON_GetObjectItem(e, "lastCheat");
      d->launch_count = cJSON_IsNumber(lc) ? (unsigned int)lc->valueint : 0;
      d->last_launch = cJSON_IsNumber(ll) ? (time_t)ll->valuedouble : 0;
      d->total_seconds = cJSON_IsNumber(ts) ? (unsigned int)ts->valueint : 0;
      if (cJSON_IsString(lch)) {
        snprintf(d->last_cheat, sizeof(d->last_cheat), "%s", lch->valuestring);
      }
    }
  }
  pthread_mutex_unlock(&g_activity_lock);
  cJSON_Delete(root);
}
