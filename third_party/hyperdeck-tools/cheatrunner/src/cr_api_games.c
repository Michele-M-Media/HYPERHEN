#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_games.h"
#include "cr_favorites.h"

int
cr_api_games_handle(int fd, const char *method, const char *path,
                     const char *query, const char *body, size_t body_len) {
  (void)method; (void)body; (void)body_len;
  if (!strcmp(path, "/api/health")) {
    handle_api_health(fd);
    return 1;
  }
  if (!strcmp(path, "/api/status")) {
    handle_api_status(fd);
    return 1;
  }
  if (!strcmp(path, "/api/state")) {
    handle_api_state(fd);
    return 1;
  }
  if (!strcmp(path, "/api/running")) {
    handle_api_running(fd);
    return 1;
  }
  if (!strcmp(path, "/api/debug/process")) {
    handle_api_debug_process(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/launch/status")) {
    handle_api_launch_status(fd);
    return 1;
  }
  if (!strcmp(path, "/api/games") || !strcmp(path, "/appdb")) {
    handle_api_games(fd, query);
    return 1;
  }
  if (!strcmp(path, "/appdb/icon")) {
    handle_appdb_icon(fd, query);
    return 1;
  }
  if (!strcmp(path, "/appdb/lookup")) {
    handle_appdb_lookup(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/titles/lookup") || !strcmp(path, "/api/appdb/lookup")) {
    handle_api_titles_lookup(fd, query);
    return 1;
  }
  if (!strcmp(path, "/appdb/pic0")) {
    handle_appdb_pic0(fd, query);
    return 1;
  }
  if (!strcmp(path, "/launch") || !strcmp(path, "/api/launch")) {
    handle_launch(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/notifications")) {
    handle_api_notifications(fd);
    return 1;
  }
  if (!strcmp(path, "/api/notifications/read")) {
    handle_api_notifications_read(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/notifications/clear")) {
    handle_api_notifications_clear(fd);
    return 1;
  }
  if (!strcmp(path, "/api/activity")) {
    handle_api_activity(fd);
    return 1;
  }
  if (!strcmp(path, "/api/activity/title")) {
    handle_api_activity_title(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/activity/reset")) {
    handle_api_activity_reset(fd);
    return 1;
  }
  if (!strcmp(path, "/api/user/context")) {
    handle_api_user_context(fd);
    return 1;
  }
  if (!strcmp(path, "/api/diag/title")) {
    handle_api_diag_title(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/favorites")) {
    handle_api_favorites_get(fd);
    return 1;
  }
  if (!strcmp(path, "/api/favorites/set")) {
    handle_api_favorites_set(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/favorites/recent")) {
    handle_api_favorites_recent(fd, query);
    return 1;
  }
  return 0;
}
