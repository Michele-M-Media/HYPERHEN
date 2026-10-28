#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/cJSON.h"
#include "cr_api_internal.h"
#include "cr_paths.h"
#include "cr_favorites.h"

#define FAV_MAX     256
#define RECENT_MAX  12
#define ID_LEN      16

static char g_fav[FAV_MAX][ID_LEN];
static int  g_fav_n = 0;
static char g_recent[RECENT_MAX][ID_LEN];
static int  g_recent_n = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Title IDs are short alphanumerics; reject anything else so a bad request cannot poison the file. */
static int
id_ok(const char *id) {
  if (!id || !id[0]) return 0;
  size_t n = strlen(id);
  if (n >= ID_LEN) return 0;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return 0;
  }
  return 1;
}

/* Must be called with g_lock held. */
static void
save_locked(void) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return;
  cJSON *fa = cJSON_AddArrayToObject(root, "favorites");
  cJSON *ra = cJSON_AddArrayToObject(root, "recents");
  if (fa) for (int i = 0; i < g_fav_n; i++)    cJSON_AddItemToArray(fa, cJSON_CreateString(g_fav[i]));
  if (ra) for (int i = 0; i < g_recent_n; i++) cJSON_AddItemToArray(ra, cJSON_CreateString(g_recent[i]));
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) return;
  write_file_atomic(CHEATRUNNER_FAVORITES_PATH, (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
favorites_load(void) {
  char *txt = NULL;
  pthread_mutex_lock(&g_lock);
  g_fav_n = 0;
  g_recent_n = 0;
  if (read_file_text(CHEATRUNNER_FAVORITES_PATH, &txt) == 0 && txt) {
    cJSON *root = cJSON_Parse(txt);
    if (root) {
      cJSON *fa = cJSON_GetObjectItem(root, "favorites");
      if (cJSON_IsArray(fa)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, fa) {
          if (g_fav_n >= FAV_MAX) break;
          if (cJSON_IsString(it) && id_ok(it->valuestring)) {
            snprintf(g_fav[g_fav_n], ID_LEN, "%s", it->valuestring);
            g_fav_n++;
          }
        }
      }
      cJSON *ra = cJSON_GetObjectItem(root, "recents");
      if (cJSON_IsArray(ra)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, ra) {
          if (g_recent_n >= RECENT_MAX) break;
          if (cJSON_IsString(it) && id_ok(it->valuestring)) {
            snprintf(g_recent[g_recent_n], ID_LEN, "%s", it->valuestring);
            g_recent_n++;
          }
        }
      }
      cJSON_Delete(root);
    }
  }
  free(txt);
  pthread_mutex_unlock(&g_lock);
}

void
handle_api_favorites_get(int fd) {
  cJSON *root = cJSON_CreateObject();
  if (root) {
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON *fa = cJSON_AddArrayToObject(root, "favorites");
    cJSON *ra = cJSON_AddArrayToObject(root, "recents");
    pthread_mutex_lock(&g_lock);
    if (fa) for (int i = 0; i < g_fav_n; i++)    cJSON_AddItemToArray(fa, cJSON_CreateString(g_fav[i]));
    if (ra) for (int i = 0; i < g_recent_n; i++) cJSON_AddItemToArray(ra, cJSON_CreateString(g_recent[i]));
    pthread_mutex_unlock(&g_lock);
  }
  if (!root) { http_send_json(fd, 500, "{\"ok\":false}"); return; }
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) { http_send_json(fd, 500, "{\"ok\":false}"); return; }
  http_send_json(fd, 200, txt);
  free(txt);
}

void
handle_api_favorites_set(int fd, const char *query) {
  char id[ID_LEN] = {0};
  char fav[8] = {0};
  if (query_value(query, "id", id, sizeof(id)) != 0 || !id_ok(id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad id\"}");
    return;
  }
  int on = (query_value(query, "fav", fav, sizeof(fav)) == 0 && atoi(fav)) ? 1 : 0;
  pthread_mutex_lock(&g_lock);
  int idx = -1;
  for (int i = 0; i < g_fav_n; i++) if (!strcmp(g_fav[i], id)) { idx = i; break; }
  if (on) {
    if (idx < 0 && g_fav_n < FAV_MAX) { snprintf(g_fav[g_fav_n], ID_LEN, "%s", id); g_fav_n++; }
  } else if (idx >= 0) {
    if (idx != g_fav_n - 1) snprintf(g_fav[idx], ID_LEN, "%s", g_fav[g_fav_n - 1]);
    g_fav_n--;
  }
  save_locked();
  pthread_mutex_unlock(&g_lock);
  http_send_json(fd, 200, "{\"ok\":true}");
}

void
handle_api_favorites_recent(int fd, const char *query) {
  char id[ID_LEN] = {0};
  if (query_value(query, "id", id, sizeof(id)) != 0 || !id_ok(id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad id\"}");
    return;
  }
  pthread_mutex_lock(&g_lock);
  int w = 0;
  for (int i = 0; i < g_recent_n; i++) {
    if (strcmp(g_recent[i], id) != 0) {
      if (w != i) snprintf(g_recent[w], ID_LEN, "%s", g_recent[i]);
      w++;
    }
  }
  g_recent_n = w;
  if (g_recent_n >= RECENT_MAX) g_recent_n = RECENT_MAX - 1;
  for (int i = g_recent_n; i > 0; i--) snprintf(g_recent[i], ID_LEN, "%s", g_recent[i - 1]);
  snprintf(g_recent[0], ID_LEN, "%s", id);
  g_recent_n++;
  save_locked();
  pthread_mutex_unlock(&g_lock);
  http_send_json(fd, 200, "{\"ok\":true}");
}
