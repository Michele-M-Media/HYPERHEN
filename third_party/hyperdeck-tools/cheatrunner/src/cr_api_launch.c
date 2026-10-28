#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cr_api_internal.h"
#include "cr_activity.h"
#include "cr_game_monitor.h"
#include "cr_launch.h"
#include "cr_paths.h"
#include "cr_titles.h"

void
handle_api_launch_status(int fd) {
  char body[1100];
  char ls_phase[32];
  char ls_title[16];
  char ls_message[256];
  char ls_method[32];
  char ls_hex[16];
  int  ls_busy, ls_rc, ls_verified, ls_fgvalid;
  uint64_t ls_started_ms, ls_updated_ms, ls_gen;

  launch_status_recover_stale();

  pthread_mutex_lock(&g_launch_status_lock);
  snprintf(ls_phase,   sizeof(ls_phase),   "%s", g_launch_status.phase);
  snprintf(ls_title,   sizeof(ls_title),   "%s", g_launch_status.title_id);
  snprintf(ls_message, sizeof(ls_message), "%s", g_launch_status.message);
  snprintf(ls_method,  sizeof(ls_method),  "%s", g_launch_status.method);
  snprintf(ls_hex,     sizeof(ls_hex),     "%s", g_launch_status.hex);
  ls_busy       = g_launch_status.busy;
  ls_rc         = g_launch_status.rc;
  ls_verified   = g_launch_status.verified;
  ls_fgvalid    = g_launch_status.foreground_user_valid;
  ls_started_ms = g_launch_status.started_ms;
  ls_updated_ms = g_launch_status.updated_ms;
  ls_gen        = g_launch_status.generation;
  pthread_mutex_unlock(&g_launch_status_lock);

  uint64_t now = now_ms();
  long long age_ms = 0;
  if (ls_busy && ls_started_ms > 0 && now >= ls_started_ms)
    age_ms = (long long)(now - ls_started_ms);
  long long updated_age_ms = (ls_updated_ms > 0 && now >= ls_updated_ms)
                             ? (long long)(now - ls_updated_ms) : 0;
  int stale = ls_busy && age_ms > 60000;

  /* If the launch failed but the game is running, report ready so the UI is not stuck. */
  if (strcmp(ls_phase, "failed") == 0 && ls_title[0] != '\0') {
    running_game_state_t gm;
    running_state_get(&gm);
    if (gm.running && strcmp(gm.title_id, ls_title) == 0) {
      snprintf(ls_phase,   sizeof(ls_phase),   "ready");
      snprintf(ls_message, sizeof(ls_message), "Game is running; previous launch return code was ignored.");
      ls_verified = 1;
      ls_busy     = 0;
    }
  }

  snprintf(body, sizeof(body),
           "{\"ok\":true,\"busy\":%s,\"phase\":\"%s\",\"titleId\":\"%s\",\"message\":\"%s\","
           "\"method\":\"%s\",\"rc\":%d,\"hex\":\"%s\",\"verified\":%s,\"foregroundUserValid\":%s,"
           "\"ageMs\":%lld,\"updatedAgeMs\":%lld,\"stale\":%s,\"generation\":%llu}",
           ls_busy ? "true" : "false", ls_phase, ls_title, ls_message,
           ls_method, ls_rc, ls_hex, ls_verified ? "true" : "false",
           ls_fgvalid ? "true" : "false", age_ms, updated_age_ms,
           stale ? "true" : "false", (unsigned long long)ls_gen);
  http_send_json(fd, 200, body);
}

void
handle_launch(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char args[512] = {0};
  char body[384];
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 ||
      !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid titleId\"}");
    return;
  }

  launch_status_recover_stale();

  pthread_mutex_lock(&g_launch_status_lock);
  if (g_launch_status.busy) {
    snprintf(body, sizeof(body),
             "{\"ok\":false,\"titleId\":\"%s\",\"error\":\"launch busy\",\"phase\":\"%s\",\"message\":\"%s\"}",
             g_launch_status.title_id, g_launch_status.phase, g_launch_status.message);
    pthread_mutex_unlock(&g_launch_status_lock);
    http_send_json(fd, 409, body);
    return;
  }
  pthread_mutex_unlock(&g_launch_status_lock);

  launch_worker_request_t *req = malloc(sizeof(*req));
  if (!req) {
    http_send_oom(fd);
    return;
  }
  snprintf(req->title_id, sizeof(req->title_id), "%s", title_id);
  if (query_value(query, "args", args, sizeof(args)) == 0) {
    snprintf(req->args, sizeof(req->args), "%s", args);
  } else {
    req->args[0] = '\0';
  }
  launch_begin_ex(title_id, &req->generation);
  pthread_t t;
  if (pthread_create(&t, NULL, launch_worker_thread, req) != 0) {
    free(req);
    set_launch_status(0, "failed", title_id, "thread failed", 0);
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"thread\"}");
    return;
  }
  pthread_detach(t);

  pthread_mutex_lock(&g_activity_lock);
  g_activity_launch_count++;
  g_activity_last_launch = time(NULL);
  snprintf(g_activity_last_played_title_id, sizeof(g_activity_last_played_title_id), "%s", title_id);
  int idx = activity_find_title_index_locked(title_id);
  if (idx < 0 && g_activity_titles_count < (int)(sizeof(g_activity_titles) / sizeof(g_activity_titles[0]))) {
    idx = g_activity_titles_count++;
    memset(&g_activity_titles[idx], 0, sizeof(g_activity_titles[idx]));
    snprintf(g_activity_titles[idx].title_id, sizeof(g_activity_titles[idx].title_id), "%s", title_id);
  }
  if (idx >= 0) {
    g_activity_titles[idx].launch_count++;
    g_activity_titles[idx].last_launch = g_activity_last_launch;
  }
  pthread_mutex_unlock(&g_activity_lock);
  activity_save();

  snprintf(body, sizeof(body), "{\"ok\":true,\"titleId\":\"%s\",\"busy\":true,\"phase\":\"killing_current\",\"message\":\"Queued\"}",
           title_id);
  http_send_json(fd, 200, body);
}
