#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ps5/kernel.h>

#include "ps5sdk_compat.h"
#include "jb.h"
#include "priv_bootstrap.h"
#include "pt.h"
#include "third_party/cJSON.h"
#include "third_party/stb_image.h"
#include "third_party/stb_image_write.h"
#include "cr_log.h"
#include "cr_paths.h"
#include "cr_config.h"
#include "cr_notifications.h"
#include "cr_activity.h"
#include "cr_titles.h"
#include "cr_appdb.h"
#include "cr_game_monitor.h"
#include "cr_launch.h"
#include "cr_memory.h"
#include "cr_json.h"
#include "cr_cheat_formats.h"
#include "cr_version.h"
#include "cr_cheats.h"
#include "cr_cheat_profiles.h"
#include "cr_patch_profiles.h"
#include "cr_patch_parser.h"
#include "cr_http.h"
#include "cr_repo_mirror.h"
#include "cr_api.h"
#include "cr_api_internal.h"

/* The dashboard polls cheat state every few seconds; its per-mod diagnostics are logged about once a minute. */
static __thread int t_state_verbose;

static int
state_log_due(void) {
  static time_t last;
  time_t now = time(NULL);
  if (now - last < 60) return 0;
  last = now;
  return 1;
}

#include "cr_conflict.h"
#include "cr_api_dashboard.h"
#include "cr_api_games.h"
#include "cr_api_cheats.h"
#include "cr_api_cheat_profiles.h"
#include "cr_api_patch_profiles.h"
#include "cr_api_patches.h"
#include "cr_api_sources.h"
#include "cr_api_dev.h"
#include "cr_api_logs.h"
#include "cr_shutdown.h"
#include "cr_store_lookup.h"
#include "http_client.h"
#if CHEATRUNNER_HAVE_BROWSER_OPEN
#include "cr_browser.h"
#endif

#ifndef CHEATRUNNER_VERSION
#define CHEATRUNNER_VERSION "0.18"
#endif

#define MAX_GAMES CR_APPDB_MAX_GAMES

const char g_dashboard_html[] =
#include "dashboard_html.inc"
;

const char g_dashboard_css[] =
#include "dashboard_css.inc"
;

const char g_dashboard_js[] =
#include "dashboard_js.inc"
;

#include "cheatrunner_png.h"

static int read_title_lookup_cache_name(const char *title_id, char *out, size_t out_size);
static int write_title_lookup_cache_name(const char *title_id, const char *name);
static int
url_decode(char *dst, size_t dst_size, const char *src, size_t src_len) {
  size_t o = 0;
  for (size_t i = 0; i < src_len; i++) {
    if (o + 1 >= dst_size) {
      return -1;
    }
    if (src[i] == '+') {
      dst[o++] = ' ';
      continue;
    }
    if (src[i] == '%' && i + 2 < src_len) {
      int hi = isdigit((unsigned char)src[i + 1]) ? src[i + 1] - '0' : (tolower((unsigned char)src[i + 1]) - 'a' + 10);
      int lo = isdigit((unsigned char)src[i + 2]) ? src[i + 2] - '0' : (tolower((unsigned char)src[i + 2]) - 'a' + 10);
      if (hi < 0 || hi > 15 || lo < 0 || lo > 15) {
        return -1;
      }
      dst[o++] = (char)((hi << 4) | lo);
      i += 2;
      continue;
    }
    dst[o++] = src[i];
  }
  dst[o] = '\0';
  return 0;
}

int
query_value(const char *query, const char *key, char *out, size_t out_size) {
  if (!query || !key || !out || out_size == 0) {
    return -1;
  }
  size_t key_len = strlen(key);
  const char *p = query;
  while (*p) {
    const char *amp = strchr(p, '&');
    size_t part_len = amp ? (size_t)(amp - p) : strlen(p);
    const char *eq = memchr(p, '=', part_len);
    if (eq) {
      size_t k_len = (size_t)(eq - p);
      if (k_len == key_len && strncmp(p, key, key_len) == 0) {
        const char *val = eq + 1;
        size_t val_len = part_len - (k_len + 1);
        return url_decode(out, out_size, val, val_len);
      }
    }
    if (!amp) {
      break;
    }
    p = amp + 1;
  }
  return -1;
}

static const char *
status_text_for(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 409:
    return "Conflict";
  case 413:
    return "Payload Too Large";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  case 502:
    return "Bad Gateway";
  case 503:
    return "Service Unavailable";
  default:
    return "OK";
  }
}

static int
socket_send_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, p + sent, len - sent, 0);
    if (n > 0) {
      sent += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return -1;
  }
  return 0;
}

void
http_send_response(int fd, int status, const char *content_type, const uint8_t *body, size_t body_len) {
  char header[512];
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: no-cache\r\n"
                   "\r\n",
                   status, status_text_for(status), content_type ? content_type : "application/octet-stream",
                   (unsigned int)body_len);
  if (n > 0) {
    if (socket_send_all(fd, header, (size_t)n) != 0) {
      return;
    }
    if (body_len > 0) {
      (void)socket_send_all(fd, body, body_len);
    }
  }
}

void
http_send_response_cached(int fd, int status, const char *content_type, const uint8_t *body, size_t body_len) {
  char header[512];
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: public, max-age=3600\r\n"
                   "\r\n",
                   status, status_text_for(status), content_type ? content_type : "application/octet-stream",
                   (unsigned int)body_len);
  if (n > 0) {
    if (socket_send_all(fd, header, (size_t)n) != 0) {
      return;
    }
    if (body_len > 0) {
      (void)socket_send_all(fd, body, body_len);
    }
  }
}

void
http_send_response_gzip(int fd, int status, const char *content_type,
                        const uint8_t *body, size_t body_len, int cacheable) {
  char header[512];
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Encoding: gzip\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: %s\r\n"
                   "\r\n",
                   status, status_text_for(status), content_type ? content_type : "application/octet-stream",
                   (unsigned int)body_len, cacheable ? "public, max-age=3600" : "no-cache");
  if (n > 0) {
    if (socket_send_all(fd, header, (size_t)n) != 0) {
      return;
    }
    if (body_len > 0) {
      (void)socket_send_all(fd, body, body_len);
    }
  }
}

void
http_send_json(int fd, int status, const char *body) {
  http_send_response(fd, status, "application/json", (const uint8_t *)body, strlen(body));
}

void
http_send_oom(int fd) {
  http_send_json(fd, 500, "{\"ok\":false,\"error\":\"oom\"}");
}

static int
read_title_lookup_cache_name(const char *title_id, char *out, size_t out_size) {
  if (!title_id || !out || out_size == 0) {
    return 0;
  }
  out[0] = '\0';
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.txt", CHEATRUNNER_CACHE_TITLE_NAMES_DIR, title_id);
  char *txt = NULL;
  if (read_file_text(path, &txt) != 0 || !txt) {
    free(txt);
    return 0;
  }
  str_trim(txt);
  if (!txt[0] || cr_title_name_is_unresolved(title_id, txt)) {
    free(txt);
    return 0;
  }
  snprintf(out, out_size, "%s", txt);
  free(txt);
  return 1;
}

static int
write_title_lookup_cache_name(const char *title_id, const char *name) {
  if (!title_id || !name || !name[0] || cr_title_name_is_unresolved(title_id, name)) {
    return -1;
  }
  ensure_dir_recursive(CHEATRUNNER_CACHE_TITLE_NAMES_DIR);
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.txt", CHEATRUNNER_CACHE_TITLE_NAMES_DIR, title_id);
  return write_file_atomic(path, (const uint8_t *)name, strlen(name));
}

void
handle_api_status(int fd) {
  char title[32];
  char body[512];
  int http_port = CHEATRUNNER_HTTP_PORT;
  pthread_mutex_lock(&g_cfg_lock);
  http_port = g_cfg.http_port;
  pthread_mutex_unlock(&g_cfg_lock);
  get_current_title(title, sizeof(title));
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"titleId\":\"%s\",\"httpPort\":%d,"
           "\"browserUrl\":\"http://%s:%d/\"}",
           title, http_port, g_listen_ip, http_port);
  http_send_json(fd, 200, body);
}

void
handle_api_running(int fd) {
  running_game_state_t st;
  if (read_running_state(&st) != 0 || !st.running) {
    http_send_json(fd, 200, "{\"ok\":true,\"running\":false}");
    return;
  }
  char name[512];
  char cv[128];
  char av[128];
  char body[1400];
  char *name_esc = json_escape(st.title_name);
  snprintf(name, sizeof(name), "%s", name_esc ? name_esc : st.title_name);
  free(name_esc);
  char *cv_esc = json_escape(st.content_version);
  char *av_esc = json_escape(st.app_version);
  snprintf(cv, sizeof(cv), "%s", cv_esc ? cv_esc : st.content_version);
  snprintf(av, sizeof(av), "%s", av_esc ? av_esc : st.app_version);
  free(cv_esc);
  free(av_esc);
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"running\":true,\"titleId\":\"%s\",\"titleName\":\"%s\",\"pid\":%d,"
           "\"appId\":\"0x%X\",\"imageBase\":\"0x%lx\",\"platform\":\"%s\","
           "\"contentVersion\":\"%s\",\"appVersion\":\"%s\",\"startedAt\":%llu}",
           st.title_id, name, (int)st.pid, (unsigned int)st.app_id, (long)st.image_base, st.platform, cv, av,
           (unsigned long long)st.started_at);
  http_send_json(fd, 200, body);
}

void
handle_api_debug_process(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  running_game_state_t st;
  if (read_running_state(&st) != 0 || !st.running || strcmp(st.title_id, title_id) != 0) {
    http_send_json(fd, 200, "{\"ok\":true,\"titleId\":\"\",\"running\":false}");
    return;
  }
  char body[1024];
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"titleId\":\"%s\",\"running\":true,\"pid\":%d,\"appId\":\"0x%X\","
           "\"imageBase\":\"0x%lx\",\"ebootPath\":\"%s\",\"contentVersion\":\"%s\",\"appVersion\":\"%s\"}",
           st.title_id, (int)st.pid, (unsigned int)st.app_id, (long)st.image_base, st.eboot_path, st.content_version,
           st.app_version);
  http_send_json(fd, 200, body);
}

static volatile long long g_startup_ms     = 0;
static pthread_mutex_t   g_games_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static char             *g_games_cache_json = NULL;
static size_t            g_games_cache_len  = 0;
static int               g_games_scan_busy  = 0;
static long long         g_games_cache_ts_ms = 0;
static long long         g_games_last_refresh_req_ms = 0;

/* Mismatch warn throttle: suppress repeated warns for the same (title, mod) within 60s */
#define MISMATCH_WARN_SLOTS 64
#define MISMATCH_WARN_INTERVAL_MS 60000ULL
static struct {
  char     title_id[16];
  int      mod_index;
  uint64_t last_ms;
} g_mismatch_warn_slots[MISMATCH_WARN_SLOTS];
static pthread_mutex_t g_mismatch_warn_lock = PTHREAD_MUTEX_INITIALIZER;

/* cheat_select log dedup: only log when selection state changes */
static struct {
  char title_id[10];
  char cheat_path[256];
  char detected_ver[64];
  int  version_match;
} g_cheat_select_last = {0};
static pthread_mutex_t g_cheat_select_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *
mismatch_warn_level(const char *title_id, int mod_index) {
  uint64_t now = now_ms();
  int oldest = 0;
  const char *level;
  pthread_mutex_lock(&g_mismatch_warn_lock);
  uint64_t oldest_ms = g_mismatch_warn_slots[0].last_ms;
  for (int i = 0; i < MISMATCH_WARN_SLOTS; i++) {
    if (g_mismatch_warn_slots[i].mod_index == mod_index &&
        strncmp(g_mismatch_warn_slots[i].title_id, title_id, 15) == 0) {
      if (now - g_mismatch_warn_slots[i].last_ms < MISMATCH_WARN_INTERVAL_MS) {
        level = "debug";
      } else {
        g_mismatch_warn_slots[i].last_ms = now;
        level = "warn";
      }
      pthread_mutex_unlock(&g_mismatch_warn_lock);
      return level;
    }
    if (g_mismatch_warn_slots[i].last_ms < oldest_ms) {
      oldest_ms = g_mismatch_warn_slots[i].last_ms;
      oldest = i;
    }
  }
  /* New entry: evict oldest slot */
  strncpy(g_mismatch_warn_slots[oldest].title_id, title_id, 15);
  g_mismatch_warn_slots[oldest].title_id[15] = '\0';
  g_mismatch_warn_slots[oldest].mod_index = mod_index;
  g_mismatch_warn_slots[oldest].last_ms = now;
  pthread_mutex_unlock(&g_mismatch_warn_lock);
  return "warn";
}

void
handle_api_health(int fd) {
  struct timespec _now;
  clock_gettime(CLOCK_MONOTONIC, &_now);
  long long now_ms = (long long)_now.tv_sec * 1000LL + (long long)(_now.tv_nsec / 1000000L);
  long long uptime  = g_startup_ms ? (now_ms - g_startup_ms) : 0;

  pthread_mutex_lock(&g_games_cache_lock);
  int games_busy = g_games_scan_busy;
  pthread_mutex_unlock(&g_games_cache_lock);

  pthread_mutex_lock(&g_repo_mirror.lock);
  int mirror_busy = (g_repo_mirror.state == REPO_MIRROR_RUNNING);
  pthread_mutex_unlock(&g_repo_mirror.lock);

  char ls_health_phase[32];
  char ls_health_title[16];
  int  ls_health_busy;
  uint64_t ls_health_started_ms;
  pthread_mutex_lock(&g_launch_status_lock);
  ls_health_busy       = g_launch_status.busy;
  ls_health_started_ms = g_launch_status.started_ms;
  snprintf(ls_health_phase, sizeof(ls_health_phase), "%s", g_launch_status.phase);
  snprintf(ls_health_title, sizeof(ls_health_title), "%s", g_launch_status.title_id);
  pthread_mutex_unlock(&g_launch_status_lock);
  long long launch_age_ms = 0;
  if (ls_health_busy && ls_health_started_ms > 0 && now_ms >= (long long)ls_health_started_ms)
    launch_age_ms = now_ms - (long long)ls_health_started_ms;
  int launch_stale = ls_health_busy && launch_age_ms > 60000;

  int  http_active  = cr_http_active_clients();
  int  http_max     = cr_http_max_concurrent();
  long long http_tmr = cr_http_too_many_requests_count();

  char buf[640];
  snprintf(buf, sizeof(buf),
           "{\"ok\":true,\"version\":\"%s\",\"uptimeMs\":%lld,"
           "\"busy\":{\"gamesScan\":%s,\"repoMirror\":%s,\"launch\":%s},"
           "\"launch\":{\"busy\":%s,\"phase\":\"%s\",\"titleId\":\"%s\",\"ageMs\":%lld,\"stale\":%s},"
           "\"http\":{\"activeClients\":%d,\"maxConcurrent\":%d,\"tooManyRequestsCount\":%lld}}",
           CHEATRUNNER_VERSION, uptime,
           games_busy        ? "true" : "false",
           mirror_busy       ? "true" : "false",
           ls_health_busy    ? "true" : "false",
           ls_health_busy    ? "true" : "false",
           ls_health_phase, ls_health_title, launch_age_ms,
           launch_stale      ? "true" : "false",
           http_active, http_max, http_tmr);
  http_send_json(fd, 200, buf);
}

void
handle_api_logs(int fd, const char *query) {
  int limit = 200;
  int since = -1;
  char buf[32];
  buf[0] = '\0';
  if (query_value(query, "all", buf, sizeof(buf)) == 0 && atoi(buf) != 0) {
    limit = MAX_LOG_ENTRIES;
  } else if (query_value(query, "limit", buf, sizeof(buf)) == 0) {
    int v = atoi(buf);
    if (v > 0 && v <= MAX_LOG_ENTRIES) { limit = v; }
  }
  if (query_value(query, "since", buf, sizeof(buf)) == 0) {
    since = atoi(buf);
  }

  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "logs");
  cJSON_AddBoolToObject(root, "ok", 1);

  int last_seq = -1;
  int truncated = 0;
  pthread_mutex_lock(&g_log_lock);
  int eligible = 0;
  for (int i = 0; i < g_log_count; i++) {
    int idx = (g_log_start + i) % MAX_LOG_ENTRIES;
    if (since < 0 || g_logs[idx].seq > since) { eligible++; }
  }
  int skip = (eligible > limit) ? (eligible - limit) : 0;
  if (skip > 0) { truncated = 1; }
  int skipped = 0;
  for (int i = 0; i < g_log_count; i++) {
    int idx = (g_log_start + i) % MAX_LOG_ENTRIES;
    if (since >= 0 && g_logs[idx].seq <= since) { continue; }
    if (skipped < skip) { skipped++; continue; }
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "seq", g_logs[idx].seq);
    cJSON_AddNumberToObject(e, "ts", (double)g_logs[idx].ts);
    cJSON_AddStringToObject(e, "level", g_logs[idx].level);
    cJSON_AddStringToObject(e, "tag", g_logs[idx].tag);
    cJSON_AddStringToObject(e, "message", g_logs[idx].message);
    cJSON_AddItemToArray(arr, e);
    last_seq = g_logs[idx].seq;
  }
  pthread_mutex_unlock(&g_log_lock);

  cJSON_AddNumberToObject(root, "lastSeq", last_seq);
  cJSON_AddBoolToObject(root, "truncated", truncated);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) {
    http_send_oom(fd);
    return;
  }
  http_send_response(fd, 200, "application/json", (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
handle_api_logs_clear(int fd) {
  pthread_mutex_lock(&g_log_lock);
  g_log_start = 0;
  g_log_count = 0;
  pthread_mutex_unlock(&g_log_lock);
  http_send_json(fd, 200, "{\"ok\":true}");
}

void
handle_api_notifications(int fd) {
  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "notifications");
  cJSON_AddBoolToObject(root, "ok", 1);
  pthread_mutex_lock(&g_notifications_lock);
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
    http_send_oom(fd);
    return;
  }
  http_send_response(fd, 200, "application/json", (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
handle_api_notifications_read(int fd, const char *query) {
  char id_s[32] = {0};
  int mark_all = query_value(query, "all", id_s, sizeof(id_s)) == 0 && atoi(id_s) == 1;
  int wanted_id = -1;
  if (!mark_all && query_value(query, "id", id_s, sizeof(id_s)) == 0) {
    wanted_id = atoi(id_s);
  }
  pthread_mutex_lock(&g_notifications_lock);
  for (int i = 0; i < g_notifications_count; i++) {
    if (mark_all || (wanted_id > 0 && g_notifications[i].id == wanted_id)) {
      g_notifications[i].read = 1;
    }
  }
  pthread_mutex_unlock(&g_notifications_lock);
  notifications_save();
  http_send_json(fd, 200, "{\"ok\":true}");
}

void
handle_api_notifications_clear(int fd) {
  pthread_mutex_lock(&g_notifications_lock);
  g_notifications_count = 0;
  pthread_mutex_unlock(&g_notifications_lock);
  notifications_save();
  http_send_json(fd, 200, "{\"ok\":true}");
}

void
handle_api_activity(int fd) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  pthread_mutex_lock(&g_activity_lock);
  cJSON_AddNumberToObject(root, "launchCount", g_activity_launch_count);
  cJSON_AddNumberToObject(root, "lastLaunch", (double)g_activity_last_launch);
  cJSON_AddStringToObject(root, "lastPlayedTitleId", g_activity_last_played_title_id);
  cJSON_AddStringToObject(root, "lastCheatUsed", g_activity_last_cheat_used);
  cJSON *titles = cJSON_AddArrayToObject(root, "titles");
  for (int i = 0; i < g_activity_titles_count; i++) {
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "titleId", g_activity_titles[i].title_id);
    cJSON_AddNumberToObject(e, "launchCount", g_activity_titles[i].launch_count);
    cJSON_AddNumberToObject(e, "lastLaunch", (double)g_activity_titles[i].last_launch);
    cJSON_AddNumberToObject(e, "totalSeconds", g_activity_titles[i].total_seconds);
    cJSON_AddStringToObject(e, "lastCheat", g_activity_titles[i].last_cheat);
    cJSON_AddItemToArray(titles, e);
  }
  pthread_mutex_unlock(&g_activity_lock);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) {
    http_send_oom(fd);
    return;
  }
  http_send_response(fd, 200, "application/json", (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
handle_api_activity_title(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 ||
      !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", 1);
  cJSON_AddStringToObject(root, "titleId", title_id);
  pthread_mutex_lock(&g_activity_lock);
  int idx = activity_find_title_index_locked(title_id);
  if (idx >= 0) {
    cJSON_AddNumberToObject(root, "launchCount", g_activity_titles[idx].launch_count);
    cJSON_AddNumberToObject(root, "lastLaunch", (double)g_activity_titles[idx].last_launch);
    cJSON_AddNumberToObject(root, "totalSeconds", g_activity_titles[idx].total_seconds);
    cJSON_AddStringToObject(root, "lastCheat", g_activity_titles[idx].last_cheat);
  } else {
    cJSON_AddNumberToObject(root, "launchCount", 0);
    cJSON_AddNumberToObject(root, "lastLaunch", 0);
    cJSON_AddNumberToObject(root, "totalSeconds", 0);
    cJSON_AddStringToObject(root, "lastCheat", "");
  }
  pthread_mutex_unlock(&g_activity_lock);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) {
    http_send_oom(fd);
    return;
  }
  http_send_response(fd, 200, "application/json", (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
handle_api_activity_reset(int fd) {
  pthread_mutex_lock(&g_activity_lock);
  g_activity_launch_count = 0;
  g_activity_last_launch = 0;
  g_activity_titles_count = 0;
  g_activity_last_played_title_id[0] = '\0';
  g_activity_last_cheat_used[0] = '\0';
  pthread_mutex_unlock(&g_activity_lock);
  activity_save();
  http_send_json(fd, 200, "{\"ok\":true}");
}

void
handle_api_state(int fd) {
  running_game_state_t rs;
  running_state_get(&rs);
  int local_count = 0;
  int has_cheat = 0;
  int cheat_kind = 0;
  const char *cheat_fmt = "";
  char cheat_path[256];
  if (rs.running && find_cheat_file_for_title(rs.title_id, cheat_path, sizeof(cheat_path), &cheat_kind)) {
    has_cheat = 1;
    cheat_fmt = cheat_kind == 1 ? "json" : (cheat_kind == 2 ? "shn" : "mc4");
  }
  DIR *d = opendir(CHEATRUNNER_CHEATS_DIR);
  if (d) {
    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
      if (ent->d_name[0] == '.') {
        continue;
      }
      if (recognised_cheat_extension(ent->d_name)) {
        local_count++;
      }
    }
    closedir(d);
  }
  int http_port = CHEATRUNNER_HTTP_PORT;
  int cheat_engine = 1;
  int auto_load = 1;
  int auto_download = 0;
  int allow_force_enable = 0;
  int cheat_state_delay_ms = 8000;
  int dev_reload_enabled = 0;
  int dev_shutdown_delay_ms = 700;
  pthread_mutex_lock(&g_cfg_lock);
  http_port = g_cfg.http_port;
  cheat_engine = g_cfg.cheat_engine;
  auto_load = g_cfg.auto_load_cheat_menu;
  auto_download = g_cfg.auto_download_missing_cheat;
  allow_force_enable = g_cfg.allow_force_enable;
  cheat_state_delay_ms = g_cfg.cheat_state_after_launch_delay_ms;
  dev_reload_enabled = g_cfg.dev_reload_enabled;
  dev_shutdown_delay_ms = g_cfg.dev_shutdown_delay_ms;
  pthread_mutex_unlock(&g_cfg_lock);
  char last_local[128];
  pthread_mutex_lock(&g_activity_lock);
  snprintf(last_local, sizeof(last_local), "%s", g_activity_last_cheat_used);
  pthread_mutex_unlock(&g_activity_lock);
  char *name_esc = json_escape(rs.title_name);
  char *last_esc = json_escape(last_local);
  /* Build the cheatFormat value once. */
  char cheat_format_field[16];
  if (has_cheat) snprintf(cheat_format_field, sizeof(cheat_format_field), "\"%s\"", cheat_fmt);
  else           strcpy(cheat_format_field, "null");
  int can_patch = cr_priv_can_patch_game_memory();
  char body[4096];
  if (rs.running) {
    snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"version\":\"%s\",\"browserUrl\":\"http://%s:%d/\",\"httpPort\":%d,"
        "\"cheatSource\":\"local\",\"running\":{\"running\":true,\"titleId\":\"%s\",\"titleName\":\"%s\","
        "\"platform\":\"%s\",\"appId\":\"0x%X\",\"pid\":%d,\"imageBase\":\"0x%lx\",\"contentVersion\":\"%s\","
        "\"appVersion\":\"%s\",\"startedAt\":%llu,\"hasCheat\":%s,\"cheatFormat\":%s},"
        "\"cheats\":{\"engine\":%s,\"activeCount\":0,\"lastApplied\":\"%s\",\"localCount\":%d,\"canPatchGameMemory\":%s},"
        "\"config\":{\"autoLoadCheatMenu\":%s,"
        "\"autoDownloadMissingCheat\":%s,\"allowForceEnable\":%s,\"cheatStateAfterLaunchDelayMs\":%d},"
        "\"dev\":{\"reloadEnabled\":%s,\"shutdownDelayMs\":%d}}",
        CHEATRUNNER_VERSION, g_listen_ip, http_port, http_port, rs.title_id, name_esc ? name_esc : rs.title_name,
        rs.platform, rs.app_id, (int)rs.pid, (long)rs.image_base, rs.content_version, rs.app_version,
        (unsigned long long)rs.started_at, has_cheat ? "true" : "false",
        cheat_format_field, cheat_engine ? "true" : "false", last_esc ? last_esc : "", local_count,
        can_patch ? "true" : "false",
        auto_load ? "true" : "false", auto_download ? "true" : "false", allow_force_enable ? "true" : "false",
        cheat_state_delay_ms, dev_reload_enabled ? "true" : "false", dev_shutdown_delay_ms);
  } else {
    snprintf(
        body, sizeof(body),
        "{\"ok\":true,\"version\":\"%s\",\"browserUrl\":\"http://%s:%d/\",\"httpPort\":%d,"
        "\"cheatSource\":\"local\",\"running\":{\"running\":false},"
        "\"cheats\":{\"engine\":%s,\"activeCount\":0,\"lastApplied\":\"%s\",\"localCount\":%d,\"canPatchGameMemory\":%s},"
        "\"config\":{\"autoLoadCheatMenu\":%s,"
        "\"autoDownloadMissingCheat\":%s,\"allowForceEnable\":%s,\"cheatStateAfterLaunchDelayMs\":%d},"
        "\"dev\":{\"reloadEnabled\":%s,\"shutdownDelayMs\":%d}}",
        CHEATRUNNER_VERSION, g_listen_ip, http_port, http_port, cheat_engine ? "true" : "false",
        last_esc ? last_esc : "", local_count, can_patch ? "true" : "false",
        auto_load ? "true" : "false", auto_download ? "true" : "false",
        allow_force_enable ? "true" : "false", cheat_state_delay_ms, dev_reload_enabled ? "true" : "false",
        dev_shutdown_delay_ms);
  }
  /* Splice in appdb diagnostics before final closing brace */
  {
    char amode[32] = {0}, areason[64] = {0};
    size_t alast = 0;
    appdb_diag_get(amode, sizeof(amode), areason, sizeof(areason), &alast);
    size_t blen = strlen(body);
    if (blen > 0 && body[blen - 1] == '}') {
      char *aesc = json_escape(areason);
      int compiled = CHEATRUNNER_HAVE_SQLITE_APPDB;
      int is_sqlite = (strcmp(amode, "sqlite") == 0);
      char appdb_frag[384];
      snprintf(appdb_frag, sizeof(appdb_frag),
               ",\"appdb\":{\"compiled\":%s,\"mode\":\"%s\",\"fallback\":%s,\"lastCount\":%u,\"reason\":\"%s\"}}",
               compiled ? "true" : "false",
               amode[0] ? amode : "unknown",
               is_sqlite ? "false" : "true",
               (unsigned)alast,
               aesc ? aesc : "");
      free(aesc);
      if (blen - 1 + strlen(appdb_frag) < sizeof(body)) {
        snprintf(body + blen - 1, sizeof(body) - blen + 1, "%s", appdb_frag);
      }
    }
  }
  free(last_esc);
  free(name_esc);
  http_send_json(fd, 200, body);
}

/* Returns length of the games JSON array written into buf (does NOT include NUL). */
static size_t
build_games_json_array(char *buf, size_t cap, int debug_names) {
  game_entry_t *entries = calloc(MAX_GAMES, sizeof(*entries));
  if (!entries) {
    if (cap >= 3) { buf[0] = '['; buf[1] = ']'; buf[2] = '\0'; }
    return 2;
  }
  size_t count = 0;
  appdb_collect_games(entries, &count);
  char running_title[32];
  get_current_title(running_title, sizeof(running_title));
  /* Move the active game to the top of the list */
  if (running_title[0]) {
    for (size_t i = 1; i < count; i++) {
      if (strcmp(running_title, entries[i].title_id) == 0) {
        game_entry_t tmp = entries[i];
        memmove(&entries[1], &entries[0], i * sizeof(game_entry_t));
        entries[0] = tmp;
        break;
      }
    }
  }
  size_t off = 0;
  off += (size_t)snprintf(buf + off, cap - off, "[");
  size_t emitted = 0;
  for (size_t i = 0; i < count; i++) {
    if (!is_game_title_id(entries[i].title_id)) continue;
    char ver[32] = "";
    char content_id[96] = "";
    char icon_path[512];
    char pic0_path[512];
    char cheat_path[256];
    int cheat_kind = 0;
    int has_icon = resolve_icon_path(entries[i].title_id, icon_path, sizeof(icon_path)) == 0;
    int has_pic0 = resolve_pic0_path(entries[i].title_id, pic0_path, sizeof(pic0_path)) == 0;
    int has_cheat = find_cheat_file_for_title(entries[i].title_id, cheat_path, sizeof(cheat_path), &cheat_kind);
    char patch_xml_path[256] = "";
    int has_patch = (patch_find_xml_for_title(entries[i].title_id, patch_xml_path, sizeof(patch_xml_path)) == 0);
    int is_app = cr_title_is_known_media_app(entries[i].title_id, entries[i].title_name);
    const char *platform = is_app ? "APP" : platform_for_title_id(entries[i].title_id);
    int running = strcmp(running_title, entries[i].title_id) == 0;
    int autoload_cheats = cheat_profiles_has(entries[i].title_id);
    int autoload_patches = patch_profiles_has(entries[i].title_id);
    const char *cheat_fmt = has_cheat ? (cheat_kind == 1 ? "json" : (cheat_kind == 2 ? "shn" : "mc4")) : "";
    if (read_param_value_by_title_id(entries[i].title_id, "contentVersion", ver, sizeof(ver)) != 0)
      read_param_value_by_title_id(entries[i].title_id, "appVersion", ver, sizeof(ver));
    if (!ver[0])
      read_param_value_from_appmeta(entries[i].title_id, "contentVersion", ver, sizeof(ver));
    if (entries[i].content_id[0])
      snprintf(content_id, sizeof(content_id), "%s", entries[i].content_id);
    else
      read_param_value_by_title_id(entries[i].title_id, "contentId", content_id, sizeof(content_id));
    char *name_esc = json_escape(entries[i].title_name);
    char *ver_esc  = json_escape(ver[0] ? ver : "unknown");
    char *cid_esc  = json_escape(content_id[0] ? content_id : "");
    if (!name_esc) { free(ver_esc); free(cid_esc); continue; }
    off += (size_t)snprintf(
        buf + off, cap - off,
        "%s{\"titleId\":\"%s\",\"titleName\":\"%s\",\"platform\":\"%s\",\"kind\":\"%s\",\"isApp\":%s,"
        "\"version\":\"%s\","
        "\"contentId\":\"%s\",\"icon\":%s,\"iconUrl\":\"/appdb/icon?id=%s\",\"pic0\":%s,"
        "\"pic0Url\":\"/appdb/pic0?id=%s\",\"hasCheat\":%s,\"cheatFormat\":\"%s\",\"hasPatch\":%s,\"running\":%s,"
        "\"autoloadCheats\":%s,\"autoloadPatches\":%s,"
        "\"playTime\":%llu,\"lastAccessTime\":%llu",
        emitted == 0 ? "" : ",", entries[i].title_id, name_esc, platform,
        is_app ? "app" : "game", is_app ? "true" : "false",
        ver_esc ? ver_esc : "unknown", cid_esc ? cid_esc : "",
        has_icon ? "true" : "false", entries[i].title_id, has_pic0 ? "true" : "false", entries[i].title_id,
        has_cheat ? "true" : "false", cheat_fmt, has_patch ? "true" : "false", running ? "true" : "false",
        autoload_cheats ? "true" : "false", autoload_patches ? "true" : "false",
        (unsigned long long)entries[i].play_time_seconds,
        (unsigned long long)entries[i].last_access_time);
    if (debug_names) {
      char *src_esc = json_escape(entries[i].name_source[0] ? entries[i].name_source : "fallback_title_id");
      off += (size_t)snprintf(buf + off, cap - off, ",\"nameSource\":\"%s\",\"nameResolved\":%s",
                              src_esc ? src_esc : "fallback_title_id",
                              entries[i].name_resolved ? "true" : "false");
      free(src_esc);
    }
    off += (size_t)snprintf(buf + off, cap - off, "}");
    emitted++;
    free(name_esc); free(ver_esc); free(cid_esc);
    if (off + 256 >= cap) break;
  }
  off += (size_t)snprintf(buf + off, cap - off, "]");
  free(entries);
  return off;
}

static void *
games_scan_thread(void *arg) {
  (void)arg;
  size_t cap = 262144;
  char *arr = malloc(cap);
  if (!arr) {
    pthread_mutex_lock(&g_games_cache_lock);
    g_games_scan_busy = 0;
    pthread_mutex_unlock(&g_games_cache_lock);
    return NULL;
  }
  size_t arr_len = build_games_json_array(arr, cap, 0);
  pthread_mutex_lock(&g_games_cache_lock);
  free(g_games_cache_json);
  g_games_cache_json = arr;
  g_games_cache_len  = arr_len;
  g_games_cache_ts_ms = (long long)now_ms();
  g_games_scan_busy  = 0;
  pthread_mutex_unlock(&g_games_cache_lock);
  return NULL;
}

void
handle_api_games(int fd, const char *query) {
  int debug_names = 0;
  char dbg_q[8] = {0};
  if (query_value(query, "debug", dbg_q, sizeof(dbg_q)) == 0 && atoi(dbg_q) != 0)
    debug_names = 1;

  /* debug mode: bypass cache, scan synchronously */
  if (debug_names) {
    size_t cap = 262144;
    char *arr = malloc(cap);
    if (!arr) { http_send_oom(fd); return; }
    size_t arr_len = build_games_json_array(arr, cap, 1);
    size_t resp_cap = arr_len + 32;
    char *resp = malloc(resp_cap);
    if (!resp) { free(arr); http_send_oom(fd); return; }
    size_t resp_len = (size_t)snprintf(resp, resp_cap, "{\"ok\":true,\"games\":%.*s}", (int)arr_len, arr);
    http_send_response(fd, 200, "application/json", (const uint8_t *)resp, resp_len);
    free(resp); free(arr);
    return;
  }

  char refresh_q[8] = {0};
  int force_refresh = (query_value(query, "refresh", refresh_q, sizeof(refresh_q)) == 0 && atoi(refresh_q) != 0);
  if (force_refresh) patch_index_invalidate();
  const int refresh_min_interval_ms = 5000;
  int ttl_ms = 30000;
  pthread_mutex_lock(&g_cfg_lock);
  ttl_ms = g_cfg.games_cache_ttl_ms;
  pthread_mutex_unlock(&g_cfg_lock);
  if (ttl_ms < 1000 || ttl_ms > 300000) {
    ttl_ms = 30000;
  }
  long long now = (long long)now_ms();

  int need_start = 0;
  int throttled = 0;
  long long cache_age_ms = -1;
  pthread_mutex_lock(&g_games_cache_lock);
  int has_cache = (g_games_cache_json != NULL);
  if (has_cache && g_games_cache_ts_ms > 0 && now >= g_games_cache_ts_ms) {
    cache_age_ms = now - g_games_cache_ts_ms;
  }

  int cache_fresh = (has_cache && cache_age_ms >= 0 && cache_age_ms < ttl_ms);
  if (!g_games_scan_busy) {
    if (!has_cache) {
      g_games_scan_busy = 1;
      g_games_last_refresh_req_ms = now;
      need_start = 1;
    } else if (force_refresh) {
      if (g_games_last_refresh_req_ms > 0 && (now - g_games_last_refresh_req_ms) < refresh_min_interval_ms) {
        throttled = 1;
      } else {
        g_games_scan_busy = 1;
        g_games_last_refresh_req_ms = now;
        need_start = 1;
      }
    } else if (!cache_fresh) {
      g_games_scan_busy = 1;
      g_games_last_refresh_req_ms = now;
      need_start = 1;
    }
  }
  int busy = g_games_scan_busy;
  pthread_mutex_unlock(&g_games_cache_lock);

  if (need_start) {
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, games_scan_thread, NULL) != 0) {
      pthread_mutex_lock(&g_games_cache_lock);
      g_games_scan_busy = 0;
      busy = 0;
      if (!g_games_cache_json) {
        cache_age_ms = -1;
      }
      pthread_mutex_unlock(&g_games_cache_lock);
    }
    pthread_attr_destroy(&attr);
  }

  pthread_mutex_lock(&g_games_cache_lock);
  char *arr_copy = NULL;
  size_t arr_len = 0;
  if (g_games_cache_json) {
    arr_len  = g_games_cache_len;
    arr_copy = malloc(arr_len + 1);
    if (arr_copy) { memcpy(arr_copy, g_games_cache_json, arr_len); arr_copy[arr_len] = '\0'; }
    if (g_games_cache_ts_ms > 0 && now >= g_games_cache_ts_ms) {
      cache_age_ms = now - g_games_cache_ts_ms;
    }
  }
  pthread_mutex_unlock(&g_games_cache_lock);

  if (!arr_copy) {
    const char *msg = busy
      ? "{\"ok\":true,\"games\":[],\"refreshing\":true,\"cacheAgeMs\":-1,\"throttled\":false}"
      : "{\"ok\":true,\"games\":[],\"refreshing\":false,\"cacheAgeMs\":-1,\"throttled\":false}";
    http_send_json(fd, 200, msg);
    return;
  }

  if (cache_age_ms < 0) {
    cache_age_ms = 0;
  }
  size_t resp_cap = arr_len + 128;
  char *resp = malloc(resp_cap);
  if (!resp) { free(arr_copy); http_send_oom(fd); return; }
  size_t resp_len = (size_t)snprintf(resp, resp_cap,
                                     "{\"ok\":true,\"games\":%.*s,\"refreshing\":%s,\"cacheAgeMs\":%lld,\"throttled\":%s}",
                                     (int)arr_len, arr_copy, busy ? "true" : "false",
                                     cache_age_ms, throttled ? "true" : "false");
  http_send_response(fd, 200, "application/json", (const uint8_t *)resp, resp_len);
  free(resp);
  free(arr_copy);
}

void
handle_appdb_lookup(int fd, const char *query) {
  char title_id[32] = {0};
  char upper_id[16] = {0};
  char name[256] = {0};
  char source[64] = "local";
  char body[1024];
  int cache_enabled = 1;

  if (query_value(query, "id", title_id, sizeof(title_id)) != 0) {
    query_value(query, "titleId", title_id, sizeof(title_id));
  }
  if (!title_id[0] || !title_id_normalize(title_id, upper_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad id\"}");
    return;
  }
  if (!is_valid_title_id(upper_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"unsupported title id\"}");
    return;
  }

  pthread_mutex_lock(&g_cfg_lock);
  cache_enabled = g_cfg.title_lookup_cache_enabled;
  pthread_mutex_unlock(&g_cfg_lock);

  if (cache_enabled && read_title_lookup_cache_name(upper_id, name, sizeof(name))) {
    snprintf(source, sizeof(source), "%s", "cache_lookup");
    cr_log("debug", "title.lookup", "cache hit title=%s name=\"%s\"", upper_id, name);
  }

  if (cr_title_name_is_unresolved(upper_id, name)) {
    /* Heap-allocate — 1024 × 404 bytes ≈ 404 KB would overflow the thread stack. */
    game_entry_t *entries = calloc(MAX_GAMES, sizeof(*entries));
    if (entries) {
      size_t count = 0;
      appdb_collect_games(entries, &count);
      for (size_t i = 0; i < count; i++) {
        if (!strcmp(entries[i].title_id, upper_id) &&
            !cr_title_name_is_unresolved(upper_id, entries[i].title_name)) {
          snprintf(name, sizeof(name), "%s", entries[i].title_name);
          snprintf(source, sizeof(source), "%s", entries[i].name_source[0] ? entries[i].name_source : "local");
          break;
        }
      }
      free(entries);
    }
  }

  if (cr_title_name_is_unresolved(upper_id, name) &&
      read_param_value_by_title_id(upper_id, "titleName", name, sizeof(name)) == 0 &&
      !cr_title_name_is_unresolved(upper_id, name)) {
    snprintf(source, sizeof(source), "%s", "param_json");
  }

  /* PlayStation Store lookup as last resort before falling back to the title ID string. */
  if (cr_title_name_is_unresolved(upper_id, name)) {
    int store_enabled = 0;
    int store_timeout_ms = 3000;
    pthread_mutex_lock(&g_cfg_lock);
    store_enabled  = g_cfg.title_lookup_enabled;
    store_timeout_ms = g_cfg.title_lookup_timeout_ms > 0 ? g_cfg.title_lookup_timeout_ms : 3000;
    pthread_mutex_unlock(&g_cfg_lock);
    if (store_enabled) {
      char cid[96] = {0};
      read_param_value_by_title_id(upper_id, "contentId", cid, sizeof(cid));
      if (cid[0] && cr_store_lookup(cid, name, sizeof(name), store_timeout_ms)) {
        snprintf(source, sizeof(source), "ps_store");
        cr_log("info", "title.lookup", "store hit title=%s name=\"%s\"", upper_id, name);
      }
    }
  }

  if (cr_title_name_is_unresolved(upper_id, name)) {
    snprintf(name, sizeof(name), "%s", upper_id);
    snprintf(source, sizeof(source), "%s", "fallback_title_id");
  } else if (cache_enabled && strcmp(source, "cache_lookup") != 0) {
    write_title_lookup_cache_name(upper_id, name);
  }

  char *name_esc = json_escape(name);
  snprintf(body, sizeof(body), "{\"ok\":true,\"id\":\"%s\",\"titleId\":\"%s\",\"name\":\"%s\",\"source\":\"%s\"}",
           upper_id, upper_id, name_esc ? name_esc : "", source);
  free(name_esc);
  http_send_json(fd, 200, body);
}

void
handle_api_titles_lookup(int fd, const char *query) {
  handle_appdb_lookup(fd, query);
}

static void
jpg_append(void *ctx, void *data, int size) {
  FILE *f = ctx;
  fwrite(data, 1, (size_t)size, f);
}

/* Box-downscales the 512px icon to 256px JPEG (about 20 KB), cached next to the PNG. */
static int
icon_thumb_ensure(const char *png_path, char *out, size_t out_sz) {
  struct stat st;
  snprintf(out, out_sz, "%s.t.jpg", png_path);
  if (stat(out, &st) == 0 && st.st_size > 0) {
    return 0;
  }
  uint8_t *buf = NULL;
  size_t len = 0;
  if (read_file_bytes(png_path, &buf, &len) != 0) {
    return -1;
  }
  int w = 0, h = 0, n = 0;
  uint8_t *px = stbi_load_from_memory(buf, (int)len, &w, &h, &n, 3);
  free(buf);
  if (!px) {
    return -1;
  }
  int f = w / 256;
  int rc = -1;
  if (f >= 2 && h / f > 0) {
    int tw = w / f, th = h / f;
    uint8_t *small = malloc((size_t)tw * th * 3);
    if (small) {
      for (int y = 0; y < th; y++) {
        for (int x = 0; x < tw; x++) {
          for (int c = 0; c < 3; c++) {
            int sum = 0;
            for (int dy = 0; dy < f; dy++) {
              for (int dx = 0; dx < f; dx++) {
                sum += px[(((size_t)y * f + dy) * w + (size_t)x * f + dx) * 3 + c];
              }
            }
            small[((size_t)y * tw + x) * 3 + c] = (uint8_t)(sum / (f * f));
          }
        }
      }
      char tmp[560];
      snprintf(tmp, sizeof(tmp), "%s.tmp", out);
      FILE *fp = fopen(tmp, "wb");
      if (fp) {
        int ok = stbi_write_jpg_to_func(jpg_append, fp, tw, th, 3, small, 85);
        fclose(fp);
        rc = (ok && rename(tmp, out) == 0) ? 0 : -1;
      }
      free(small);
    }
  }
  stbi_image_free(px);
  return rc;
}

void
handle_appdb_icon(int fd, const char *query) {
  char title_id[32] = {0};
  char norm_id[10] = {0};
  char path[512];
  uint8_t *buf = NULL;
  size_t len = 0;
  if (query_value(query, "id", title_id, sizeof(title_id)) != 0 || !title_id_normalize(title_id, norm_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad id\"}");
    return;
  }
  if (cache_media_ensure(norm_id, 0, path, sizeof(path)) != 0 || read_file_bytes(path, &buf, &len) != 0) {
    http_send_json(fd, 404, "{\"ok\":false,\"error\":\"no icon\"}");
    return;
  }
  char thumb[560], flag[4];
  if (query_value(query, "t", flag, sizeof(flag)) == 0 && icon_thumb_ensure(path, thumb, sizeof(thumb)) == 0) {
    uint8_t *tb = NULL;
    size_t tl = 0;
    if (read_file_bytes(thumb, &tb, &tl) == 0) {
      http_send_response_cached(fd, 200, "image/jpeg", tb, tl);
      free(tb);
      free(buf);
      return;
    }
  }
  http_send_response_cached(fd, 200, "image/png", buf, len);
  free(buf);
}

void
handle_appdb_pic0(int fd, const char *query) {
  char title_id[32] = {0};
  char norm_id[10] = {0};
  char path[512];
  uint8_t *buf = NULL;
  size_t len = 0;
  if (query_value(query, "id", title_id, sizeof(title_id)) != 0 || !title_id_normalize(title_id, norm_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad id\"}");
    return;
  }
  if (cache_media_ensure(norm_id, 1, path, sizeof(path)) != 0 || read_file_bytes(path, &buf, &len) != 0) {
    http_send_json(fd, 404, "{\"ok\":false,\"error\":\"no pic0\"}");
    return;
  }
  http_send_response_cached(fd, 200, "image/png", buf, len);
  free(buf);
}

void
handle_api_cheats_index(int fd) {
  ensure_data_dirs();

  cJSON *root = cJSON_CreateObject();
  cJSON *files = cJSON_AddArrayToObject(root, "files");
  if (!root || !files) {
    cJSON_Delete(root);
    http_send_oom(fd);
    return;
  }
  cJSON_AddBoolToObject(root, "ok", 1);

  DIR *d = opendir(CHEATRUNNER_CHEATS_DIR);
  if (d) {
    struct dirent *ent = NULL;
    char seen[512][16];
    int seen_n = 0;
    while ((ent = readdir(d)) != NULL) {
      const char *name = ent->d_name;
      if (name[0] == '.') {
        continue;
      }
      int kind = recognised_cheat_extension(name);
      if (!kind || !is_safe_filename(name)) {
        continue;
      }
      char id[16] = {0};
      if (!extract_title_id_prefix(name, id, sizeof(id))) {
        continue;
      }

      int dup = 0;
      for (int i = 0; i < seen_n; i++) {
        if (!strcmp(seen[i], id)) {
          dup = 1;
          break;
        }
      }
      if (dup) {
        continue;
      }
      if (seen_n < (int)(sizeof(seen) / sizeof(seen[0]))) {
        snprintf(seen[seen_n], sizeof(seen[seen_n]), "%s", id);
        seen_n++;
      }

      char best_path[256];
      int best_kind = 0;
      char display[256];
      display[0] = '\0';
      if (find_cheat_file_for_title(id, best_path, sizeof(best_path), &best_kind)) {
        read_cheat_display_name(best_path, best_kind, display, sizeof(display));
      } else {
        best_kind = kind;
      }

      const char *fmt = best_kind == 1 ? "json" : (best_kind == 2 ? "shn" : "mc4");
      cJSON *e = cJSON_CreateObject();
      cJSON_AddStringToObject(e, "titleId", id);
      cJSON_AddStringToObject(e, "name", display[0] ? display : id);
      cJSON_AddStringToObject(e, "format", fmt);
      cJSON_AddItemToArray(files, e);
    }
    closedir(d);
  }

  pid_t pid = -1;
  intptr_t base = 0;
  char running[16] = {0};
  if (get_running_game(&pid, running, sizeof(running), &base) == 0) {
    cJSON *running_obj = cJSON_AddObjectToObject(root, "running");
    if (running_obj) {
      char base_hex[32];
      snprintf(base_hex, sizeof(base_hex), "0x%lx", (long)base);
      cJSON_AddStringToObject(running_obj, "titleId", running);
      cJSON_AddNumberToObject(running_obj, "pid", pid);
      cJSON_AddStringToObject(running_obj, "imageBase", base_hex);
    }
  } else {
    cJSON_AddNullToObject(root, "running");
  }

  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!body) {
    http_send_oom(fd);
    return;
  }
  http_send_response(fd, 200, "application/json", (const uint8_t *)body, strlen(body));
  free(body);
}

void
handle_api_cheats_get(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};

  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 ||
      !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }

  /* Optional ?format=json|shn|mc4 — lets the UI pick a specific file. */
  char fmt_s[8] = {0};
  int req_kind = 0;
  query_value(query, "format", fmt_s, sizeof(fmt_s));
  if (!strcasecmp(fmt_s, "json"))      req_kind = 1;
  else if (!strcasecmp(fmt_s, "shn"))  req_kind = 2;
  else if (!strcasecmp(fmt_s, "mc4"))  req_kind = 3;

  /* Collect all candidates to build the formats list and pick a file. */
  cheat_file_search_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  find_cheat_candidates(title_id, &ctx);

  if (!ctx.best_path[0]) {
    http_send_json(fd, 404, "{\"ok\":false,\"error\":\"no cheat file\"}");
    return;
  }

  /* Derive set of available format kinds from the candidates list. */
  int avail_kinds[4] = {0}; /* indexed by kind 1..3 */
  for (int i = 0; i < ctx.candidate_count; i++) {
    int k = ctx.candidates[i].kind;
    if (k >= 1 && k <= 3) avail_kinds[k] = 1;
  }

  /* Select file: requested format if available, otherwise the best candidate. */
  char path[256] = {0};
  int kind = 0;
  if (req_kind && avail_kinds[req_kind]) {
    for (int i = 0; i < ctx.candidate_count; i++) {
      if (ctx.candidates[i].kind == req_kind) {
        snprintf(path, sizeof(path), "%s", ctx.candidates[i].path);
        kind = req_kind;
        break;
      }
    }
  }
  if (!path[0]) {
    snprintf(path, sizeof(path), "%s", ctx.best_path);
    kind = ctx.best_kind;
  }

  char *txt = NULL;
  if (read_file_text(path, &txt) != 0 || !txt) {
    http_send_json(fd, 404, "{\"ok\":false,\"error\":\"could not read cheat file\"}");
    return;
  }

  /* Convert to JSON string. */
  static const char * const kname[] = {NULL, "json", "shn", "mc4"};
  char *json = NULL;
  if (kind == 1) {
    json = txt; txt = NULL;
  } else if (kind == 2) {
    json = shn_xml_to_json(txt, strlen(txt));
    free(txt); txt = NULL;
    if (!json) { http_send_json(fd, 500, "{\"ok\":false,\"error\":\"SHN parse failed\"}"); return; }
  } else {
    char *xml = mc4_decrypt_to_xml(txt, strlen(txt), NULL);
    free(txt); txt = NULL;
    if (!xml) { http_send_json(fd, 500, "{\"ok\":false,\"error\":\"MC4 decrypt failed\"}"); return; }
    json = shn_xml_to_json(xml, strlen(xml));
    free(xml);
    if (!json) { http_send_json(fd, 500, "{\"ok\":false,\"error\":\"MC4 XML parse failed\"}"); return; }
  }

  /* Build formats JSON array: e.g. ["json","shn"] */
  char fmts_json[64];
  int fp = 0;
  fmts_json[fp++] = '[';
  int fmts_first = 1;
  for (int k = 1; k <= 3; k++) {
    if (!avail_kinds[k]) continue;
    if (!fmts_first) fmts_json[fp++] = ',';
    fmts_first = 0;
    fmts_json[fp++] = '"';
    size_t kl = strlen(kname[k]);
    memcpy(fmts_json + fp, kname[k], kl); fp += (int)kl;
    fmts_json[fp++] = '"';
  }
  fmts_json[fp++] = ']'; fmts_json[fp] = 0;

  /* Inject ,\"formats\":[...],"activeFormat":"..." before the final '}'. */
  char inject[128];
  int inj = snprintf(inject, sizeof(inject),
    ",\"formats\":%s,\"activeFormat\":\"%s\"}",
    fmts_json, (kind >= 1 && kind <= 3) ? kname[kind] : "json");
  char *end = strrchr(json, '}');
  if (!end || inj <= 0) {
    http_send_response(fd, 200, "application/json", (const uint8_t *)json, strlen(json));
    free(json);
    return;
  }
  *end = '\0';
  size_t base_len = (size_t)(end - json);
  char *out = malloc(base_len + (size_t)inj + 1);
  if (!out) {
    free(json);
    http_send_oom(fd);
    return;
  }
  memcpy(out, json, base_len);
  memcpy(out + base_len, inject, (size_t)inj + 1);
  free(json);
  http_send_response(fd, 200, "application/json", (const uint8_t *)out, base_len + (size_t)inj);
  free(out);
}

/* Single-slot cache of the decoded JSON, keyed by (title_id, path, mtime, kind),
 * so state polls do not re-decrypt MC4/SHN. */
static struct {
  pthread_mutex_t lock;
  char title_id[10];
  char path[256];
  time_t mtime;
  int kind;
  char *json_txt;
} g_cheat_doc_cache = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* Conflict map cache: rebuilt when cheat path or mtime changes. */
static struct {
  pthread_mutex_t    lock;
  char               path[256];
  time_t             mtime;
  cheat_conflict_map_t map;
  int                ready;
} g_conflict_cache = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* Rebuild g_conflict_cache when the cheat file changes; needs the doc cache loaded first. */
static void
conflict_cache_ensure(const char *path, time_t mtime) {
  /* Check+build under one lock so two concurrent callers can't both double-build (and leak entries_p). */
  pthread_mutex_lock(&g_conflict_cache.lock);
  if (g_conflict_cache.ready &&
      g_conflict_cache.mtime == mtime &&
      strcmp(g_conflict_cache.path, path) == 0) {
    pthread_mutex_unlock(&g_conflict_cache.lock);
    return;
  }
  pthread_mutex_unlock(&g_conflict_cache.lock);

  /* Borrow the JSON text from the doc cache */
  pthread_mutex_lock(&g_cheat_doc_cache.lock);
  char *json_copy = g_cheat_doc_cache.json_txt ? strdup(g_cheat_doc_cache.json_txt) : NULL;
  pthread_mutex_unlock(&g_cheat_doc_cache.lock);

  if (!json_copy) return;

  cheat_conflict_map_t tmp;
  int built = (conflict_map_build(json_copy, &tmp) == 0);
  free(json_copy);

  if (built) {
    pthread_mutex_lock(&g_conflict_cache.lock);
    /* Re-check inside the lock: another thread may have already rebuilt */
    if (!g_conflict_cache.ready ||
        g_conflict_cache.mtime != mtime ||
        strcmp(g_conflict_cache.path, path) != 0) {
      g_conflict_cache.map   = tmp;
      g_conflict_cache.mtime = mtime;
      g_conflict_cache.ready = 1;
      snprintf(g_conflict_cache.path, sizeof(g_conflict_cache.path), "%s", path);
      cr_log("debug", "conflicts",
             "conflict map built path=%s mods=%d pairs=%d",
             path, tmp.mod_count, tmp.pair_count);
    }
    pthread_mutex_unlock(&g_conflict_cache.lock);
  }
}

static cJSON *
load_cheat_json_root_for_title_ex(const char *title_id, char *err, size_t err_size, char *path_out, size_t path_out_size,
                                  int *kind_out) {
  char path[256];
  int kind = 0;
  if (!find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) {
    snprintf(err, err_size, "no cheat file");
    return NULL;
  }

  /* Stat the file to get mtime for cache validation */
  struct stat st_buf;
  time_t file_mtime = 0;
  if (stat(path, &st_buf) == 0) {
    file_mtime = st_buf.st_mtime;
  }

  /* Check cache: if title_id + path + mtime + kind all match, parse from cached text */
  pthread_mutex_lock(&g_cheat_doc_cache.lock);
  if (g_cheat_doc_cache.json_txt &&
      g_cheat_doc_cache.kind == kind &&
      g_cheat_doc_cache.mtime == file_mtime && file_mtime != 0 &&
      strcmp(g_cheat_doc_cache.path, path) == 0 &&
      strcmp(g_cheat_doc_cache.title_id, title_id) == 0) {
    char *cached = strdup(g_cheat_doc_cache.json_txt);
    pthread_mutex_unlock(&g_cheat_doc_cache.lock);
    if (!cached) {
      snprintf(err, err_size, "cache alloc failed");
      return NULL;
    }
    cJSON *root = cJSON_Parse(cached);
    free(cached);
    if (!root) {
      snprintf(err, err_size, "json parse failed (cache)");
      return NULL;
    }
    if (path_out && path_out_size > 0) snprintf(path_out, path_out_size, "%s", path);
    if (kind_out) *kind_out = kind;
    return root;
  }
  pthread_mutex_unlock(&g_cheat_doc_cache.lock);

  /* Cache miss: load from disk */
  char *txt = NULL;
  if (read_file_text(path, &txt) != 0 || !txt) {
    snprintf(err, err_size, "could not read cheat file");
    return NULL;
  }
  char *json_txt = NULL;
  if (kind == 1) {
    json_txt = txt;
  } else if (kind == 2) {
    json_txt = shn_xml_to_json(txt, strlen(txt));
    free(txt);
  } else {
    char *xml = mc4_decrypt_to_xml(txt, strlen(txt), NULL);
    free(txt);
    if (!xml) {
      snprintf(err, err_size, "MC4 decrypt failed");
      return NULL;
    }
    json_txt = shn_xml_to_json(xml, strlen(xml));
    free(xml);
  }
  if (!json_txt) {
    snprintf(err, err_size, "parse failed");
    return NULL;
  }

  /* Update cache */
  char *cache_copy = strdup(json_txt);
  if (cache_copy) {
    pthread_mutex_lock(&g_cheat_doc_cache.lock);
    free(g_cheat_doc_cache.json_txt);
    g_cheat_doc_cache.json_txt = cache_copy;
    g_cheat_doc_cache.kind     = kind;
    g_cheat_doc_cache.mtime    = file_mtime;
    snprintf(g_cheat_doc_cache.path,     sizeof(g_cheat_doc_cache.path),     "%s", path);
    snprintf(g_cheat_doc_cache.title_id, sizeof(g_cheat_doc_cache.title_id), "%s", title_id);
    pthread_mutex_unlock(&g_cheat_doc_cache.lock);
  }

  cJSON *root = cJSON_Parse(json_txt);
  free(json_txt);
  if (!root) {
    snprintf(err, err_size, "json parse failed");
    return NULL;
  }
  if (path_out && path_out_size > 0) {
    snprintf(path_out, path_out_size, "%s", path);
  }
  if (kind_out) {
    *kind_out = kind;
  }
  return root;
}

static cJSON *
load_cheat_json_root_for_title(const char *title_id, char *err, size_t err_size) {
  return load_cheat_json_root_for_title_ex(title_id, err, err_size, NULL, 0, NULL);
}

typedef enum cheat_state_kind {
  CHEAT_STATE_OFF = 0,
  CHEAT_STATE_ON,
  CHEAT_STATE_MIXED,
  CHEAT_STATE_MISMATCH,
  CHEAT_STATE_GAME_NOT_RUNNING,
  CHEAT_STATE_PROCESS_NOT_FOUND,
  CHEAT_STATE_BASE_NOT_READY,
  CHEAT_STATE_READ_FAILED,
  CHEAT_STATE_INVALID_CHEAT,
  CHEAT_STATE_GAME_LOADING,
  CHEAT_STATE_BASELINE_UNKNOWN,
  CHEAT_STATE_ADDRESS_UNRESOLVED,
  CHEAT_STATE_FORMAT_NEEDS_REVIEW,
  CHEAT_STATE_OFF_UNVERIFIED,
  CHEAT_STATE_ON_UNVERIFIED,
  CHEAT_STATE_CRASH_SUSPECT,
  CHEAT_STATE_CONFLICT,
  CHEAT_STATE_UNKNOWN
} cheat_state_kind_t;

static const char *
cheat_state_key(cheat_state_kind_t s) {
  switch (s) {
  case CHEAT_STATE_OFF:
    return "off";
  case CHEAT_STATE_ON:
    return "on";
  case CHEAT_STATE_MIXED:
    return "mixed";
  case CHEAT_STATE_MISMATCH:
    return "mismatch";
  case CHEAT_STATE_GAME_NOT_RUNNING:
    return "game_not_ready";
  case CHEAT_STATE_PROCESS_NOT_FOUND:
    return "process_not_found";
  case CHEAT_STATE_BASE_NOT_READY:
    return "base_not_ready";
  case CHEAT_STATE_READ_FAILED:
    return "read_failed";
  case CHEAT_STATE_INVALID_CHEAT:
    return "invalid_cheat";
  case CHEAT_STATE_GAME_LOADING:
    return "game_loading";
  case CHEAT_STATE_BASELINE_UNKNOWN:
    return "baseline_unknown";
  case CHEAT_STATE_ADDRESS_UNRESOLVED:
    return "address_unresolved";
  case CHEAT_STATE_FORMAT_NEEDS_REVIEW:
    return "format_needs_review";
  case CHEAT_STATE_OFF_UNVERIFIED:
    return "off_unverified";
  case CHEAT_STATE_ON_UNVERIFIED:
    return "on_unverified";
  case CHEAT_STATE_CRASH_SUSPECT:
    return "crash_suspect";
  case CHEAT_STATE_CONFLICT:
    return "conflict";
  default:
    return "unknown";
  }
}

static const char *
cheat_state_label(cheat_state_kind_t s) {
  switch (s) {
  case CHEAT_STATE_OFF:
    return "OFF";
  case CHEAT_STATE_ON:
    return "ON";
  case CHEAT_STATE_MIXED:
    return "PARTIAL";
  case CHEAT_STATE_MISMATCH:
    return "VERSION MISMATCH";
  case CHEAT_STATE_GAME_NOT_RUNNING:
    return "GAME NOT READY";
  case CHEAT_STATE_PROCESS_NOT_FOUND:
    return "PROCESS NOT FOUND";
  case CHEAT_STATE_BASE_NOT_READY:
    return "BASE NOT READY";
  case CHEAT_STATE_READ_FAILED:
    return "READ FAILED";
  case CHEAT_STATE_INVALID_CHEAT:
    return "INVALID CHEAT";
  case CHEAT_STATE_GAME_LOADING:
    return "WAITING FOR GAME";
  case CHEAT_STATE_BASELINE_UNKNOWN:
    return "BASELINE UNKNOWN";
  case CHEAT_STATE_ADDRESS_UNRESOLVED:
    return "ADDRESS UNRESOLVED";
  case CHEAT_STATE_FORMAT_NEEDS_REVIEW:
    return "FORMAT NEEDS REVIEW";
  case CHEAT_STATE_OFF_UNVERIFIED:
    return "OFF";
  case CHEAT_STATE_ON_UNVERIFIED:
    return "ON";
  case CHEAT_STATE_CRASH_SUSPECT:
    return "CRASH SUSPECT";
  case CHEAT_STATE_CONFLICT:
    return "CONFLICT";
  default:
    return "UNKNOWN";
  }
}

void
handle_api_cheats_state(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char err[128] = {0};
  char cheat_path[256] = {0};
  int cheat_kind = 0;
  int debug_mode = 0;
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  {
    char dbg[8] = {0};
    if (query_value(query, "debug", dbg, sizeof(dbg)) == 0 && atoi(dbg) != 0) {
      debug_mode = 1;
    }
  }
  cJSON *root = load_cheat_json_root_for_title_ex(title_id, err, sizeof(err), cheat_path, sizeof(cheat_path), &cheat_kind);
  if (!root) {
    char body[256];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err);
    http_send_json(fd, 404, body);
    return;
  }
  t_state_verbose = state_log_due();
  if (t_state_verbose) cr_log("debug", "cheats", "state request title=%s debug=%d", title_id, debug_mode);
  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods)) {
    cJSON_Delete(root);
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid mods\"}");
    return;
  }

  running_game_state_t st;
  int running_ok = (read_running_state(&st) == 0 && st.running);
  int title_match = running_ok && strcmp(st.title_id, title_id) == 0;
  pid_t pid = running_ok ? st.pid : -1;
  intptr_t base = running_ok ? st.image_base : 0;
  int attach_ok = 0;
  int is_ps2_st = 0;
  int global_state_delay_ms = 0;
  pthread_mutex_lock(&g_cfg_lock);
  global_state_delay_ms = g_cfg.cheat_state_after_launch_delay_ms;
  pthread_mutex_unlock(&g_cfg_lock);
  int in_loading_window = 0;
  {
    uint64_t last_verified = __atomic_load_n(&g_last_launch_verified_at_ms, __ATOMIC_ACQUIRE);
    if (title_match && global_state_delay_ms > 0 && last_verified > 0) {
      uint64_t age = now_ms() - last_verified;
      if (age < (uint64_t)global_state_delay_ms) {
        in_loading_window = 1;
      }
    }
  }
  int can_probe = title_match && pid > 0 && base != 0 && !in_loading_window;
  if (running_ok && t_state_verbose) {
    cr_log("debug", "cheats", "running title=%s pid=%d base=0x%lx appId=0x%X", st.title_id, (int)pid, (long)base,
           (unsigned int)st.app_id);
  }
  /* Fast liveness check: ESRCH = process is gone; EPERM = alive but unprivileged signal. */
  if (can_probe && kill(pid, 0) != 0 && errno == ESRCH) {
    can_probe = 0;
  }
  /* Skip ptrace during an apply — attaching again here races the apply thread's attach and hangs. */
  if (can_probe && g_cheat_applying) {
    can_probe = 0;
  }
  /* Probing reads are mdbg-backed and need no ptrace attach. */
  if (can_probe) {
    attach_ok = 1;
    is_ps2_st = process_is_ps2_emu(pid);
  }
  const char *fmt = cheat_kind == 1 ? "json" : (cheat_kind == 2 ? "shn" : "mc4");
  if (t_state_verbose) cr_log("debug", "cheats", "loaded file=%s format=%s mods=%d", cheat_path, fmt, cJSON_GetArraySize(mods));

  /* Determine version metadata for cheat selection diagnostics */
  char detected_ver[64] = {0};
  char cheat_ver[32] = {0};
  char detected_ver_norm[32] = {0};
  char cheat_ver_norm[32] = {0};
  int version_match = 0;
  if (running_ok) {
    if (st.content_version[0])
      snprintf(detected_ver, sizeof(detected_ver), "%s", st.content_version);
    else if (st.app_version[0])
      snprintf(detected_ver, sizeof(detected_ver), "%s", st.app_version);
  }
  {
    /* Extract version token from cheat filename using shared helper */
    const char *cfname = strrchr(cheat_path, '/');
    cfname = cfname ? cfname + 1 : cheat_path;
    cr_version_from_filename(cfname, cheat_ver, sizeof(cheat_ver));
    cr_version_normalize(cheat_ver,    cheat_ver_norm,    sizeof(cheat_ver_norm));
    cr_version_normalize(detected_ver, detected_ver_norm, sizeof(detected_ver_norm));
    version_match = (cheat_ver_norm[0] && detected_ver_norm[0] &&
                     strcmp(cheat_ver_norm, detected_ver_norm) == 0) ? 1 : 0;
  }
  {
    const char *dv = detected_ver[0] ? detected_ver : "unknown";
    pthread_mutex_lock(&g_cheat_select_lock);
    if (strcmp(g_cheat_select_last.title_id,   title_id)   != 0 ||
        strcmp(g_cheat_select_last.cheat_path,  cheat_path) != 0 ||
        strcmp(g_cheat_select_last.detected_ver, dv)        != 0 ||
        g_cheat_select_last.version_match != version_match) {
      cr_log("debug", "cheat_select",
             "[game] title=%s detectedVer=%s cheatVer=%s versionMatch=%d cheatPath=%s",
             title_id, dv, cheat_ver[0] ? cheat_ver : "unknown", version_match, cheat_path);
      snprintf(g_cheat_select_last.title_id,    sizeof(g_cheat_select_last.title_id),    "%s", title_id);
      snprintf(g_cheat_select_last.cheat_path,  sizeof(g_cheat_select_last.cheat_path),  "%s", cheat_path);
      snprintf(g_cheat_select_last.detected_ver,sizeof(g_cheat_select_last.detected_ver),"%s", dv);
      g_cheat_select_last.version_match = version_match;
    }
    pthread_mutex_unlock(&g_cheat_select_lock);
  }

  /* Rebuild conflict map if cheat file changed (uses doc cache for JSON) */
  {
    struct stat cmap_st;
    time_t cmap_mtime = (stat(cheat_path, &cmap_st) == 0) ? cmap_st.st_mtime : 0;
    conflict_cache_ensure(cheat_path, cmap_mtime);
  }

  /* Gather all candidates for response (uses live game version for classification) */
  cheat_file_search_t cand_ctx;
  memset(&cand_ctx, 0, sizeof(cand_ctx));
  find_cheat_candidates_ex(title_id, detected_ver[0] ? detected_ver : NULL, &cand_ctx);

  cJSON *out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddStringToObject(out, "titleId", title_id);
  cJSON_AddStringToObject(out, "cheatPath", cheat_path);
  cJSON_AddStringToObject(out, "cheatFormat", fmt);
  cJSON_AddStringToObject(out, "detectedVersion", detected_ver[0] ? detected_ver : "unknown");
  cJSON_AddStringToObject(out, "selectedCheatVersion", cheat_ver[0] ? cheat_ver : "unknown");
  cJSON_AddBoolToObject(out, "versionMatch", version_match);
  {
    static const char * const sel_mode_strs[] = {"none","exact","generic","wrong_version","manual"};
    int sk = cand_ctx.selection_kind;
    const char *sel_mode = (sk >= 0 && sk <= 4) ? sel_mode_strs[sk] : "none";
    cJSON_AddStringToObject(out, "selectionMode", sel_mode);
  }
  /* candidates array */
  {
    cJSON *cands_arr = cJSON_AddArrayToObject(out, "candidates");
    if (cands_arr) {
      static const char * const match_strs[] = {"exact","wrong_version","generic","unknown"};
      for (int ci = 0; ci < cand_ctx.candidate_count; ci++) {
        cJSON *ce = cJSON_CreateObject();
        if (!ce) break;
        const char *cp = cand_ctx.candidates[ci].path;
        const char *cfn = strrchr(cp, '/');
        cfn = cfn ? cfn + 1 : cp;
        int ck = cand_ctx.candidates[ci].kind;
        const char *cfmt = ck == 1 ? "json" : (ck == 2 ? "shn" : "mc4");
        const char *cv = cand_ctx.candidates[ci].version;
        char cvn[32] = {0};
        if (cv[0]) cr_version_normalize(cv, cvn, sizeof(cvn));
        int cm = cand_ctx.candidates[ci].match;
        const char *cmatch = (cm >= 0 && cm <= 3) ? match_strs[cm] : "unknown";
        int is_selected = (cand_ctx.best_path[0] && strcmp(cp, cand_ctx.best_path) == 0);
        cJSON_AddStringToObject(ce, "path", cp);
        cJSON_AddStringToObject(ce, "filename", cfn);
        cJSON_AddStringToObject(ce, "format", cfmt);
        cJSON_AddStringToObject(ce, "version", cv[0] ? cv : "");
        cJSON_AddStringToObject(ce, "versionNormalized", cvn[0] ? cvn : "");
        cJSON_AddStringToObject(ce, "match", cmatch);
        cJSON_AddBoolToObject(ce, "selected", is_selected);
        cJSON_AddNumberToObject(ce, "score", cand_ctx.candidates[ci].score);
        cJSON_AddItemToArray(cands_arr, ce);
      }
    }
  }
  cJSON *game = cJSON_AddObjectToObject(out, "game");
  if (game) {
    cJSON_AddBoolToObject(game, "running", running_ok ? 1 : 0);
    cJSON_AddStringToObject(game, "runningTitleId", running_ok ? st.title_id : "");
    cJSON_AddNumberToObject(game, "pid", running_ok ? st.pid : -1);
    char base_hex[32];
    snprintf(base_hex, sizeof(base_hex), "0x%lx", (long)(running_ok ? st.image_base : 0));
    cJSON_AddStringToObject(game, "base", base_hex);
    cJSON_AddStringToObject(game, "contentVersion", running_ok ? st.content_version : "");
    cJSON_AddStringToObject(game, "appVersion", running_ok ? st.app_version : "");
  }
  cJSON *arr = cJSON_AddArrayToObject(out, "mods");
  cJSON *mismatches = cJSON_AddArrayToObject(out, "mismatches");
  int summary_on = 0, summary_off = 0, summary_mismatch = 0, summary_not_ready = 0, summary_baseline_unknown = 0;
  int idx = 0;
  cJSON *mod = NULL;
  cJSON_ArrayForEach(mod, mods) {
    cJSON *e = cJSON_CreateObject();
    int mod_index = idx++;
    cJSON_AddNumberToObject(e, "index", mod_index);
    cJSON *n = cJSON_GetObjectItem(mod, "name");
    const char *mod_name = cJSON_IsString(n) ? n->valuestring : "mod";
    cJSON_AddStringToObject(e, "name", mod_name);
    cJSON *mem = cJSON_GetObjectItem(mod, "memory");
    if (!cJSON_IsArray(mem)) {
      mem = cJSON_GetObjectItem(mod, "patches");
    }
    cheat_state_kind_t state = CHEAT_STATE_UNKNOWN;
    const char *reason = "";
    int off_val_matches = 0;  /* entries where mem matches ValueOff (soft OFF indicator) */
    if (!cJSON_IsArray(mem) || cJSON_GetArraySize(mem) == 0) {
      state = CHEAT_STATE_INVALID_CHEAT;
      reason = "mod has no memory entries";
    } else if (!running_ok) {
      state = CHEAT_STATE_GAME_NOT_RUNNING;
      reason = "no game is currently running";
    } else if (!title_match) {
      state = CHEAT_STATE_PROCESS_NOT_FOUND;
      reason = "running game does not match cheat title";
    } else if (in_loading_window) {
      state = CHEAT_STATE_GAME_LOADING;
      reason = "game just started";
    } else if (pid <= 0) {
      state = CHEAT_STATE_PROCESS_NOT_FOUND;
      reason = "process not found";
    } else if (base == 0) {
      state = CHEAT_STATE_BASE_NOT_READY;
      reason = "base address not ready";
    } else if (!attach_ok) {
      state = CHEAT_STATE_PROCESS_NOT_FOUND;
      reason = "pt_attach failed";
    } else {
      /* Per-mod base: respect module_name, matching apply_cheat_json behaviour. */
      intptr_t mod_base_st = base;
      {
        cJSON *mn_j_st = cJSON_GetObjectItem(mod, "module_name");
        const char *mn_st = (cJSON_IsString(mn_j_st) && mn_j_st->valuestring) ? mn_j_st->valuestring : "";
        if (mn_st[0] && resolve_module_base(pid, mn_st, base, &mod_base_st) != 0) {
          state = CHEAT_STATE_PROCESS_NOT_FOUND;
          reason = "module_not_loaded";
        }
      }
      int total = 0;
      int on_matches = 0;
      int off_matches = 0;
      int mismatch = 0;
      int baseline_unknown = 0;
      int mismatch_logged = 0;
      int ff_hook = 0;
      int any_mc_pending = 0;
      int cross_inactive_matches = 0; /* off_matches caused only by another mod owning this cave/hook */

      /* Master-code-dependent address correction, mirrors apply_cheat_json's lazy fallback. */
      int mc_avail_st = 0;
      int mc_pending_st = 0; /* 1 = this mod's mastercode is not active yet */
      uint64_t mc_base_off_st = 0;
      uint8_t *mc_on_bytes_st CR_AUTOFREE = NULL;
      size_t mc_on_len_st = 0;
      intptr_t mc_live_addr_st = 0;
      {
        cJSON *mc_mod_st = find_master_code_mod_for(mods, mod_index);
        if (mc_mod_st) {
          mc_base_off_st = mc_mod_first_offset(mc_mod_st);
          if (mc_base_off_st != 0) {
            cJSON *mc_mem0_st = cJSON_GetObjectItem(mc_mod_st, "memory");
            cJSON *mc_e0_st = (cJSON_IsArray(mc_mem0_st) && cJSON_GetArraySize(mc_mem0_st) > 0)
                              ? cJSON_GetArrayItem(mc_mem0_st, 0) : NULL;
            if (mc_e0_st) {
              cJSON *mc_on_j_st = cJSON_GetObjectItem(mc_e0_st, "on");
              if (cJSON_IsString(mc_on_j_st) && mc_on_j_st->valuestring) {
                size_t mc_need_st = hex_bytes_need(mc_on_j_st->valuestring);
                mc_on_bytes_st = malloc(mc_need_st);
                if (mc_on_bytes_st &&
                    parse_hex_bytes_checked(mc_on_j_st->valuestring, mc_on_bytes_st, mc_need_st, &mc_on_len_st) != 0)
                  mc_on_len_st = 0;
              }
            }
            mc_live_addr_st = mod_base_st + (intptr_t)mc_base_off_st;
            mc_avail_st = (mc_on_len_st > 0);
            if (mc_avail_st) {
              uint8_t *mc_cur_st CR_AUTOFREE = malloc(mc_on_len_st);
              if (!mc_cur_st || read_process_memory(pid, mc_live_addr_st, mc_cur_st, mc_on_len_st) != 0 ||
                  memcmp(mc_cur_st, mc_on_bytes_st, mc_on_len_st) != 0) {
                mc_pending_st = 1;
              }
            }
          }
        }
      }

      cJSON *m = NULL;
      cJSON_ArrayForEach(m, mem) {
        if (state != CHEAT_STATE_UNKNOWN) break;
        cJSON *off_j = cJSON_GetObjectItem(m, "offset");
        cJSON *on_j = cJSON_GetObjectItem(m, "on");
        cJSON *off_j2 = cJSON_GetObjectItem(m, "off");
        cJSON *expected_j = cJSON_GetObjectItem(m, "expected");
        cJSON *abs_j = cJSON_GetObjectItem(m, "absolute");
        if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j) || !cJSON_IsString(off_j2)) {
          state = CHEAT_STATE_INVALID_CHEAT;
          reason = "memory entry missing offset/on/off";
          break;
        }
        size_t need = hex_bytes_need(on_j->valuestring);
        if (hex_bytes_need(off_j2->valuestring) > need) need = hex_bytes_need(off_j2->valuestring);
        if (cJSON_IsString(expected_j) && hex_bytes_need(expected_j->valuestring) > need) need = hex_bytes_need(expected_j->valuestring);
        uint8_t *on_b CR_AUTOFREE = malloc(need), *off_b CR_AUTOFREE = malloc(need);
        uint8_t *exp_b CR_AUTOFREE = malloc(need), *cur CR_AUTOFREE = malloc(need);
        if (!on_b || !off_b || !exp_b || !cur) {
          state = CHEAT_STATE_INVALID_CHEAT;
          reason = "out of memory";
          break;
        }
        size_t on_len = 0, off_len = 0, exp_len = 0;
        if (parse_hex_bytes_checked(on_j->valuestring, on_b, need, &on_len) != 0 ||
            parse_hex_bytes_checked(off_j2->valuestring, off_b, need, &off_len) != 0 || on_len != off_len) {
          state = CHEAT_STATE_INVALID_CHEAT;
          reason = "invalid on/off bytes";
          break;
        }
        uint64_t off_u = 0;
        if (parse_offset_hex_checked(off_j->valuestring, &off_u) != 0) {
          state = CHEAT_STATE_INVALID_CHEAT;
          reason = "invalid offset";
          break;
        }
        if (cJSON_IsString(expected_j) && expected_j->valuestring && expected_j->valuestring[0]) {
          if (parse_hex_bytes_checked(expected_j->valuestring, exp_b, need, &exp_len) != 0 || exp_len != on_len) {
            state = CHEAT_STATE_INVALID_CHEAT;
            reason = "invalid expected bytes";
            break;
          }
        }
        /* PS2 emulation: all addresses absolute (matches apply_cheat_json). */
        int af_s = (cJSON_IsTrue(abs_j) || is_ps2_st) ? 1 : 0;
        intptr_t addr = cheat_resolve_addr(pid, mod_base_st, off_u, af_s,
                                           on_b, exp_len > 0 ? exp_b : NULL, on_len);
        /* Try absolute candidate for ADDRESS_UNRESOLVED detection */
        intptr_t abs_cand2 = (intptr_t)off_u;
        intptr_t rel_cand2 = mod_base_st + (intptr_t)off_u;
        if (t_state_verbose) cr_log("debug", "cheats.state",
               "read_state mod=%d off=0x%llx addr=0x%lx abs=0x%lx rel=0x%lx len=%zu",
               idx, (unsigned long long)off_u, (long)addr,
               (long)abs_cand2, (long)rel_cand2, on_len);
        if (read_process_memory(pid, addr, cur, on_len) != 0) {
          /* Try the other candidate before declaring unresolved */
          intptr_t alt = (addr == abs_cand2) ? rel_cand2 : abs_cand2;
          if (t_state_verbose) cr_log("debug", "cheats.state",
                 "read_state_retry mod=%d addr=0x%lx (primary failed)", idx, (long)alt);
          if (on_len <= need && read_process_memory(pid, alt, cur, on_len) == 0) {
            addr = alt;
          } else {
            cr_log("warn", "cheats.state",
                   "read_state_unresolved mod=%d off=0x%llx abs=0x%lx rel=0x%lx",
                   idx, (unsigned long long)off_u, (long)abs_cand2, (long)rel_cand2);
            state = CHEAT_STATE_ADDRESS_UNRESOLVED;
            reason = "read failed on both address candidates";
            break;
          }
        }
        total++;
        int is_on = memcmp(cur, on_b, on_len) == 0;
        /* For MC4/SHN without explicit expected bytes, off_b is NOT a reliable original */
        int off_reliable = (cheat_kind == 1) || (exp_len > 0);
        int is_off = off_reliable && (memcmp(cur, (exp_len > 0 ? exp_b : off_b), on_len) == 0);
        /* Neither matched at the plain address; retry through the master-code scan, same as apply_cheat_json. */
        if (!is_on && !is_off && mc_avail_st && off_len > 0 && !af_s) {
          intptr_t mc_scanned = 0;
          if (mc_scan_dep_addr(pid, mc_live_addr_st, mc_on_bytes_st, mc_on_len_st,
                               off_b, off_len, mc_base_off_st, off_u, &mc_scanned) &&
              mc_scanned != addr &&
              read_process_memory(pid, mc_scanned, cur, on_len) == 0) {
            addr = mc_scanned;
            is_on = memcmp(cur, on_b, on_len) == 0;
            is_off = off_reliable && (memcmp(cur, (exp_len > 0 ? exp_b : off_b), on_len) == 0);
          }
        }
        /* Another mod owns this address (cave/JMP redirect): report OFF, not a mismatch. */
        int cross_mod_inactive = 0;
        if (!is_on && !is_off && off_reliable) {
          if (on_len >= 16) {
            cross_mod_inactive = 1;   /* cave — another mod's shellcode lives here */
          } else if (on_len >= 5 && on_b[0] == 0xE9 && cur[0] == 0xE9) {
            cross_mod_inactive = 1;   /* hook redirect — a different JMP is already installed */
          }
        }
        if (is_on) {
          on_matches++;
        }
        if (is_off || cross_mod_inactive) {
          off_matches++;
          if (cross_mod_inactive) cross_inactive_matches++;
        }
        /* Its mastercode is not active yet, so the bytes are not real game state: unconfirmed baseline, not a mismatch. */
        int mc_dep_pending = mc_avail_st && mc_pending_st;
        if (!is_on && !is_off && !cross_mod_inactive && off_reliable) {
          if (mc_dep_pending) {
            baseline_unknown++;
            any_mc_pending = 1;
          } else {
            mismatch++;
            if (on_len > 0 && on_len < 16) {
              int all_ff = 1;
              for (size_t _bz = 0; _bz < on_len; _bz++) {
                if (cur[_bz] != 0xFF) { all_ff = 0; break; }
              }
              if (all_ff) ff_hook++;
            }
          }
        }
        if (!is_on && !off_reliable) {
          /* ValueOff match means disabled/never-applied; counted separately so state can be OFF_UNVERIFIED, not BASELINE_UNKNOWN. */
          if (off_len > 0 && memcmp(cur, off_b, on_len) == 0) {
            off_val_matches++;
          } else {
            baseline_unknown++;
          }
        }
        if (!is_on && !is_off && !cross_mod_inactive && off_reliable && !mc_dep_pending) {
          if (!mismatch_logged) {
            char cur_hex[512], on_hex[128], exp_hex[128], addr_hex[32], off_hex_addr[32];
            bytes_to_hex(cur, on_len > 32 ? 32 : on_len, cur_hex, sizeof(cur_hex));
            bytes_to_hex(on_b, on_len > 16 ? 16 : on_len, on_hex, sizeof(on_hex));
            bytes_to_hex(exp_len > 0 ? exp_b : off_b, on_len > 16 ? 16 : on_len, exp_hex, sizeof(exp_hex));
            snprintf(addr_hex, sizeof(addr_hex), "0x%lx", (long)addr);
            snprintf(off_hex_addr, sizeof(off_hex_addr), "0x%llX", (unsigned long long)off_u);
            /* All-printable ASCII usually means the offset landed in string data, not code. */
            int cur_is_ascii = (on_len > 0);
            for (size_t _ai = 0; _ai < on_len && cur_is_ascii; _ai++) {
              if (cur[_ai] < 0x20 || cur[_ai] > 0x7E) cur_is_ascii = 0;
            }
            cr_log(mismatch_warn_level(title_id, mod_index), "cheats",
                   "mismatch title=%s contentVersion=%s file=%s format=%s mod=\"%s\" modIndex=%d writeIndex=%d pid=%d "
                   "base=0x%lx offset=%s addr=%s expected=%s...(len=%zu) current=%s...(len=%zu) on=%s...(len=%zu)%s",
                   title_id, st.content_version[0] ? st.content_version : "unknown", cheat_path, fmt, mod_name, mod_index,
                   total - 1, (int)pid, (long)mod_base_st, off_hex_addr, addr_hex, exp_hex, on_len, cur_hex, on_len, on_hex, on_len,
                   cur_is_ascii ? " [hint: current bytes are printable ASCII — offset likely points to game string data, not code]" : "");
            mismatch_logged = 1;
          }
          if (debug_mode && mismatches && cJSON_GetArraySize(mismatches) < 32) {
            /* Cap hex field length at 32 bytes (64 hex chars) to bound JSON output size */
            size_t hex_cap = on_len > 32 ? 32 : on_len;
            char cur_hex[128], on_hex[128], off_hex[128], exp_hex[128], addr_hex[32], off_hex_addr[32];
            char abs_addr_hex[32], rel_addr_hex[32];
            char abs_cur_hex[128], rel_cur_hex[128];
            uint8_t abs_buf[32], rel_buf[32];
            intptr_t abs_cand = (intptr_t)off_u;
            intptr_t rel_cand = mod_base_st + (intptr_t)off_u;
            int abs_read = (hex_cap <= sizeof(abs_buf) && read_process_memory(pid, abs_cand, abs_buf, hex_cap) == 0);
            int rel_read = (hex_cap <= sizeof(rel_buf) && read_process_memory(pid, rel_cand, rel_buf, hex_cap) == 0);
            bytes_to_hex(cur, hex_cap, cur_hex, sizeof(cur_hex));
            bytes_to_hex(on_b, hex_cap, on_hex, sizeof(on_hex));
            bytes_to_hex(off_b, hex_cap, off_hex, sizeof(off_hex));
            bytes_to_hex(exp_len > 0 ? exp_b : off_b, hex_cap, exp_hex, sizeof(exp_hex));
            snprintf(addr_hex, sizeof(addr_hex), "0x%lx", (long)addr);
            snprintf(off_hex_addr, sizeof(off_hex_addr), "0x%llX", (unsigned long long)off_u);
            snprintf(abs_addr_hex, sizeof(abs_addr_hex), "0x%lx", (long)abs_cand);
            snprintf(rel_addr_hex, sizeof(rel_addr_hex), "0x%lx", (long)rel_cand);
            if (abs_read) { bytes_to_hex(abs_buf, hex_cap, abs_cur_hex, sizeof(abs_cur_hex)); } else { snprintf(abs_cur_hex, sizeof(abs_cur_hex), "(read failed)"); }
            if (rel_read) { bytes_to_hex(rel_buf, hex_cap, rel_cur_hex, sizeof(rel_cur_hex)); } else { snprintf(rel_cur_hex, sizeof(rel_cur_hex), "(read failed)"); }
            cJSON *mm = cJSON_CreateObject();
            cJSON_AddStringToObject(mm, "mod", mod_name);
            cJSON_AddNumberToObject(mm, "modIndex", mod_index);
            cJSON_AddNumberToObject(mm, "writeIndex", total - 1);
            cJSON_AddStringToObject(mm, "offset", off_hex_addr);
            cJSON_AddStringToObject(mm, "address", addr_hex);
            cJSON_AddStringToObject(mm, "expected", exp_hex);
            cJSON_AddStringToObject(mm, "off", off_hex);
            cJSON_AddStringToObject(mm, "current", cur_hex);
            cJSON_AddStringToObject(mm, "on", on_hex);
            /* both address candidates for diagnosis */
            cJSON *cands = cJSON_AddArrayToObject(mm, "candidates");
            if (cands) {
              cJSON *ca = cJSON_CreateObject();
              cJSON_AddStringToObject(ca, "mode", "absolute");
              cJSON_AddStringToObject(ca, "address", abs_addr_hex);
              cJSON_AddStringToObject(ca, "current", abs_cur_hex);
              cJSON_AddBoolToObject(ca, "readOk", abs_read);
              cJSON_AddItemToArray(cands, ca);
              cJSON *cr2 = cJSON_CreateObject();
              cJSON_AddStringToObject(cr2, "mode", "relative");
              cJSON_AddStringToObject(cr2, "address", rel_addr_hex);
              cJSON_AddStringToObject(cr2, "current", rel_cur_hex);
              cJSON_AddBoolToObject(cr2, "readOk", rel_read);
              cJSON_AddItemToArray(cands, cr2);
            }
            cJSON_AddItemToArray(mismatches, mm);
          }
        }
      }
      if (state == CHEAT_STATE_UNKNOWN) {
        if (on_matches == total && total > 0) {
          state = CHEAT_STATE_ON;
        } else if (on_matches > 0 && total > 0 && on_matches + cross_inactive_matches == total &&
                   off_matches == cross_inactive_matches && mismatch == 0 &&
                   baseline_unknown == 0 && off_val_matches == 0) {
          /* Leftover entries are cave/hook space owned by another active mod (e.g. a mastercode's dependents); still ON. */
          state = CHEAT_STATE_ON;
        } else if (mismatch > 0 && baseline_unknown == 0 && on_matches == 0 &&
                   off_matches == 0 && off_val_matches == 0) {
          /* All patches have reliable baseline and none match — true version mismatch */
          state = CHEAT_STATE_MISMATCH;
          reason = ff_hook > 0
            ? "hook address reads 0xFF — base+offset lands in unmapped memory; try address mode 'absolute'"
            : "current bytes match neither ON nor expected/off";
        } else if (off_matches == total && total > 0 && baseline_unknown == 0 && off_val_matches == 0) {
          state = CHEAT_STATE_OFF;
        } else if ((off_matches + off_val_matches) == total && total > 0 &&
                   baseline_unknown == 0 && mismatch == 0 && off_val_matches > 0) {
          /* All entries' ValueOff bytes match — cheat is in its disabled state. */
          state = CHEAT_STATE_OFF_UNVERIFIED;
          reason = "MC4/SHN: ValueOff bytes match current memory; cheat appears disabled.";
        } else if (baseline_unknown > 0 && mismatch == 0 && on_matches == 0) {
          /* No ON matches, no reliable mismatches — not applied either way, so report BASELINE_UNKNOWN not MIXED. */
          state = CHEAT_STATE_BASELINE_UNKNOWN;
          reason = any_mc_pending
            ? "requires Master Code to be enabled first"
            : "MC4/SHN: no reliable original bytes; state cannot be confirmed";
        } else {
          state = CHEAT_STATE_MIXED;
          reason = ff_hook > 0
            ? "hook address reads 0xFF — base+offset may be in unmapped memory"
            : "some writes differ";
        }
      }
    }
    /* Promote BASELINE_UNKNOWN → OFF_UNVERIFIED when mod was explicitly disabled this session */
    if (state == CHEAT_STATE_BASELINE_UNKNOWN && can_probe &&
        mod_disabled_check(title_id, pid, mod_index)) {
      state = CHEAT_STATE_OFF_UNVERIFIED;
      reason = "Disabled using runtime backup; MC4 baseline unknown.";
    }
    /* Promote BASELINE_UNKNOWN → ON_UNVERIFIED when mod was explicitly enabled this session */
    if (state == CHEAT_STATE_BASELINE_UNKNOWN && can_probe &&
        mod_enabled_check(title_id, pid, mod_index)) {
      state = CHEAT_STATE_ON_UNVERIFIED;
      reason = "Enabled this session; MC4/SHN baseline unknown, cannot confirm bytes.";
    }
    /* Mark crash_suspect if game stopped shortly after enabling */
    {
      pthread_mutex_lock(&g_crash_guard_lock);
      for (int _ci = 0; _ci < g_crash_suspects_n; _ci++) {
        if (strcmp(g_crash_suspects[_ci].title_id, title_id) == 0 &&
            g_crash_suspects[_ci].mod_index == mod_index) {
          state = CHEAT_STATE_CRASH_SUSPECT;
          reason = "Game stopped shortly after enabling. Use 'Clear Crash Flags' to re-enable.";
          break;
        }
      }
      pthread_mutex_unlock(&g_crash_guard_lock);
    }
    /* Promote non-ON states to CONFLICT when a mod sharing address ranges is active. */
    int conflict_mods[16];
    int conflict_count = 0;
    char conflict_reason_buf[128];
    {
      pthread_mutex_lock(&g_conflict_cache.lock);
      if (g_conflict_cache.ready) {
        conflict_count = conflict_map_get_for_mod(&g_conflict_cache.map, mod_index,
                                                  conflict_mods, 16);
      }
      pthread_mutex_unlock(&g_conflict_cache.lock);
    }
    int active_conflict_mod = -1;
    if (conflict_count > 0 && can_probe &&
        state != CHEAT_STATE_ON && state != CHEAT_STATE_ON_UNVERIFIED &&
        state != CHEAT_STATE_CRASH_SUSPECT) {
      for (int _ci = 0; _ci < conflict_count; _ci++) {
        if (mod_enabled_check(title_id, pid, conflict_mods[_ci])) {
          active_conflict_mod = conflict_mods[_ci];
          break;
        }
      }
      if (active_conflict_mod >= 0 &&
          (state == CHEAT_STATE_OFF || state == CHEAT_STATE_OFF_UNVERIFIED ||
           state == CHEAT_STATE_MIXED || state == CHEAT_STATE_BASELINE_UNKNOWN ||
           state == CHEAT_STATE_MISMATCH)) {
        pthread_mutex_lock(&g_conflict_cache.lock);
        const char *cn = conflict_map_mod_name(&g_conflict_cache.map, active_conflict_mod);
        if (cn && cn[0])
          snprintf(conflict_reason_buf, sizeof(conflict_reason_buf),
                   "Conflicts with active mod: %s", cn);
        else
          snprintf(conflict_reason_buf, sizeof(conflict_reason_buf),
                   "Conflicts with active mod #%d", active_conflict_mod);
        pthread_mutex_unlock(&g_conflict_cache.lock);
        state  = CHEAT_STATE_CONFLICT;
        reason = conflict_reason_buf;
      }
    }
    /* Emit conflictsWith array for every mod that has known conflicts */
    if (conflict_count > 0) {
      cJSON *cw_arr = cJSON_AddArrayToObject(e, "conflictsWith");
      if (cw_arr) {
        pthread_mutex_lock(&g_conflict_cache.lock);
        for (int _ci = 0; _ci < conflict_count; _ci++) {
          cJSON *cw = cJSON_CreateObject();
          cJSON_AddNumberToObject(cw, "modIndex", conflict_mods[_ci]);
          const char *cn = conflict_map_mod_name(&g_conflict_cache.map, conflict_mods[_ci]);
          if (cn && cn[0]) cJSON_AddStringToObject(cw, "name", cn);
          cJSON_AddItemToArray(cw_arr, cw);
        }
        pthread_mutex_unlock(&g_conflict_cache.lock);
      }
    }
    cJSON_AddStringToObject(e, "state", cheat_state_key(state));
    cJSON_AddStringToObject(e, "label", cheat_state_label(state));
    if (reason && reason[0]) {
      cJSON_AddStringToObject(e, "reason", reason);
    }
    /* visualState + capability flags for frontend */
    {
      const char *visual_state;
      int can_enable_flag, can_disable_flag;
      if (state == CHEAT_STATE_ON || state == CHEAT_STATE_ON_UNVERIFIED) {
        visual_state = "on"; can_enable_flag = 0; can_disable_flag = 1;
      } else if (state == CHEAT_STATE_OFF || state == CHEAT_STATE_OFF_UNVERIFIED ||
                 state == CHEAT_STATE_BASELINE_UNKNOWN || state == CHEAT_STATE_MISMATCH ||
                 state == CHEAT_STATE_ADDRESS_UNRESOLVED || state == CHEAT_STATE_READ_FAILED ||
                 state == CHEAT_STATE_FORMAT_NEEDS_REVIEW) {
        /* Mismatch/unresolved stay clickable; the state only serves as a hint. */
        visual_state = "off"; can_enable_flag = 1; can_disable_flag = 0;
      } else if (state == CHEAT_STATE_CONFLICT) {
        /* Conflicting mods can still be enabled — engine auto-disables the blocker */
        visual_state = "conflict"; can_enable_flag = 1; can_disable_flag = 0;
      } else if (state == CHEAT_STATE_MIXED) {
        visual_state = "mixed"; can_enable_flag = 0; can_disable_flag = 1;
      } else if (state == CHEAT_STATE_CRASH_SUSPECT) {
        visual_state = "error"; can_enable_flag = 0; can_disable_flag = 0;
      } else {
        visual_state = "disabled"; can_enable_flag = 0; can_disable_flag = 0;
      }
      int can_toggle = can_enable_flag || can_disable_flag;
      const char *next_action = can_enable_flag ? "enable" : (can_disable_flag ? "disable" : "none");
      int next_on = can_enable_flag ? 1 : 0;
      /* block_reason: use the existing reason string when toggle is blocked */
      const char *block_reason = can_toggle ? "" : (reason && reason[0] ? reason : "");
      cJSON_AddStringToObject(e, "visualState", visual_state);
      cJSON_AddBoolToObject(e, "canEnable", can_enable_flag);
      cJSON_AddBoolToObject(e, "canDisable", can_disable_flag);
      cJSON_AddBoolToObject(e, "canToggle", can_toggle);
      cJSON_AddStringToObject(e, "nextAction", next_action);
      cJSON_AddBoolToObject(e, "nextOn", next_on);
      if (block_reason[0]) cJSON_AddStringToObject(e, "blockReason", block_reason);
    }
    if (state == CHEAT_STATE_ON || state == CHEAT_STATE_ON_UNVERIFIED) {
      summary_on++;
    } else if (state == CHEAT_STATE_OFF || state == CHEAT_STATE_OFF_UNVERIFIED) {
      summary_off++;
    } else if (state == CHEAT_STATE_MISMATCH) {
      summary_mismatch++;
    } else if (state == CHEAT_STATE_BASELINE_UNKNOWN) {
      summary_baseline_unknown++;
    } else {
      summary_not_ready++;
    }
    cJSON_AddItemToArray(arr, e);
  }
  cJSON_Delete(root);
  cJSON *summary = cJSON_AddObjectToObject(out, "summary");
  if (summary) {
    cJSON_AddNumberToObject(summary, "on", summary_on);
    cJSON_AddNumberToObject(summary, "off", summary_off);
    cJSON_AddNumberToObject(summary, "mismatch", summary_mismatch);
    cJSON_AddNumberToObject(summary, "baselineUnknown", summary_baseline_unknown);
    cJSON_AddNumberToObject(summary, "notReady", summary_not_ready);
  }
  if (t_state_verbose) cr_log("debug", "cheats", "state summary title=%s on=%d off=%d mismatch=%d baselineUnknown=%d notReady=%d file=%s format=%s pid=%d base=0x%lx",
         title_id, summary_on, summary_off, summary_mismatch, summary_baseline_unknown, summary_not_ready, cheat_path, fmt, (int)pid, (long)base);

  char *txt = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!txt) {
    http_send_oom(fd);
    return;
  }
  http_send_response(fd, 200, "application/json", (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
handle_api_cheats_clear_crash_flags(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  pthread_mutex_lock(&g_crash_guard_lock);
  int cleared = 0;
  for (int i = g_crash_suspects_n - 1; i >= 0; i--) {
    if (strcmp(g_crash_suspects[i].title_id, title_id) == 0) {
      g_crash_suspects[i] = g_crash_suspects[--g_crash_suspects_n];
      cleared++;
    }
  }
  pthread_mutex_unlock(&g_crash_guard_lock);
  if (cleared > 0) {
    crash_suspects_save();
  }
  char body[64];
  snprintf(body, sizeof(body), "{\"ok\":true,\"cleared\":%d}", cleared);
  cr_log("info", "cheats", "cleared crash flags title=%s n=%d", title_id, cleared);
  http_send_json(fd, 200, body);
}

/* POST /api/cheats/select: path must be in the current candidates list; wrong_version requires force=1. */
void
handle_api_cheats_select(int fd, const char *query) {
  char raw_tid[32] = {0}, title_id[10] = {0};
  char path_raw[512] = {0};
  char force_s[4] = {0};
  if (query_value(query, "titleId", raw_tid, sizeof(raw_tid)) != 0 ||
      !title_id_normalize(raw_tid, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  if (query_value(query, "path", path_raw, sizeof(path_raw)) != 0 || !path_raw[0]) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing path\"}");
    return;
  }
  int force = (query_value(query, "force", force_s, sizeof(force_s)) == 0 && atoi(force_s) != 0);

  /* Get current candidates to validate the path */
  running_game_state_t sel_st;
  char live_ver[64] = {0};
  if (read_running_state(&sel_st) == 0 && sel_st.running &&
      strcmp(sel_st.title_id, title_id) == 0) {
    if (sel_st.content_version[0])
      snprintf(live_ver, sizeof(live_ver), "%s", sel_st.content_version);
    else if (sel_st.app_version[0])
      snprintf(live_ver, sizeof(live_ver), "%s", sel_st.app_version);
  }
  cheat_file_search_t sel_ctx;
  memset(&sel_ctx, 0, sizeof(sel_ctx));
  find_cheat_candidates_ex(title_id, live_ver[0] ? live_ver : NULL, &sel_ctx);

  /* Check path is in candidates */
  int found_idx = -1;
  for (int i = 0; i < sel_ctx.candidate_count; i++) {
    if (strcmp(sel_ctx.candidates[i].path, path_raw) == 0) {
      found_idx = i;
      break;
    }
  }
  if (found_idx < 0) {
    cr_log("warn", "cheat_select", "manual_select rejected: path not in candidates title=%s path=%s", title_id, path_raw);
    http_send_json(fd, 403, "{\"ok\":false,\"error\":\"path not in candidate list for this title\"}");
    return;
  }
  /* Require force for wrong_version */
  if (sel_ctx.candidates[found_idx].match == CAND_MATCH_WRONG_VERSION && !force) {
    http_send_json(fd, 409,
      "{\"ok\":false,\"error\":\"version_mismatch\",\"message\":\"This file has a different version than the running game. Add force=1 to override.\"}");
    return;
  }
  /* Block a file switch while mods from the current file are active; their hooks point into caves
   * the new file would overwrite. */
  if (sel_st.running && strcmp(sel_st.title_id, title_id) == 0 && !force) {
    char cur_path[256] = {0};
    int  cur_kind = 0;
    find_cheat_file_for_title(title_id, cur_path, sizeof(cur_path), &cur_kind);
    if (cur_path[0] && strcmp(cur_path, path_raw) != 0 &&
        cheat_any_enabled_for_title(title_id, sel_st.pid)) {
      http_send_json(fd, 409,
        "{\"ok\":false,\"error\":\"active_mods_conflict\","
        "\"message\":\"Active mods from the current cheat file are still enabled. "
        "Disable all mods before switching cheat files to avoid a game crash. "
        "Add force=1 to override.\"}");
      return;
    }
  }

  if (cheat_manual_select(title_id, path_raw) != 0) {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"store failed\"}");
    return;
  }
  const char *fname = strrchr(path_raw, '/');
  fname = fname ? fname + 1 : path_raw;
  cr_log("info", "cheat_select", "manual_select title=%s path=%s match=%d force=%d",
         title_id, path_raw, sel_ctx.candidates[found_idx].match, force);
  char body[512];
  snprintf(body, sizeof(body), "{\"ok\":true,\"titleId\":\"%s\",\"path\":\"%s\",\"filename\":\"%s\"}",
           title_id, path_raw, fname);
  http_send_json(fd, 200, body);
}

/* POST /api/cheats/select/auto?titleId=CUSA00900
 * Reset to auto-selection (clear manual override). */
void
handle_api_cheats_select_auto(int fd, const char *query) {
  char raw_tid[32] = {0}, title_id[10] = {0};
  if (query_value(query, "titleId", raw_tid, sizeof(raw_tid)) != 0 ||
      !title_id_normalize(raw_tid, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  cheat_manual_select_clear(title_id);
  cr_log("info", "cheat_select", "manual_select_clear title=%s", title_id);
  char body[64];
  snprintf(body, sizeof(body), "{\"ok\":true,\"titleId\":\"%s\"}", title_id);
  http_send_json(fd, 200, body);
}

void
handle_api_cheats_debug(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char q[96];
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  snprintf(q, sizeof(q), "titleId=%s&debug=1", title_id);
  handle_api_cheats_state(fd, q);
}

static cheat_state_kind_t
check_mod_enable_state(const char *title_id, int mod_index, char *reason, size_t reason_size) {
  char err[128] = {0};
  int cheat_kind_cme = 0;
  cJSON *root = load_cheat_json_root_for_title_ex(title_id, err, sizeof(err), NULL, 0, &cheat_kind_cme);
  if (!root) {
    running_game_state_t gs_fb;
    if (read_running_state(&gs_fb) != 0 || !gs_fb.running) {
      snprintf(reason, reason_size, "no game is currently running");
      return CHEAT_STATE_GAME_NOT_RUNNING;
    }
    snprintf(reason, reason_size, "%s", err[0] ? err : "invalid cheat");
    return CHEAT_STATE_INVALID_CHEAT;
  }
  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods) || mod_index < 0 || mod_index >= cJSON_GetArraySize(mods)) {
    cJSON_Delete(root);
    snprintf(reason, reason_size, "mod index out of range");
    return CHEAT_STATE_INVALID_CHEAT;
  }
  running_game_state_t st;
  if (read_running_state(&st) != 0 || !st.running) {
    cJSON_Delete(root);
    snprintf(reason, reason_size, "no game is currently running");
    return CHEAT_STATE_GAME_NOT_RUNNING;
  }
  if (strcmp(st.title_id, title_id) != 0) {
    cJSON_Delete(root);
    snprintf(reason, reason_size, "running game does not match cheat title");
    return CHEAT_STATE_PROCESS_NOT_FOUND;
  }
  if (st.pid <= 0) {
    cJSON_Delete(root);
    snprintf(reason, reason_size, "process not found");
    return CHEAT_STATE_PROCESS_NOT_FOUND;
  }
  if (st.image_base == 0) {
    cJSON_Delete(root);
    snprintf(reason, reason_size, "base address not ready");
    return CHEAT_STATE_BASE_NOT_READY;
  }
  int delay_ms = 8000;
  pthread_mutex_lock(&g_cfg_lock);
  delay_ms = g_cfg.cheat_state_after_launch_delay_ms;
  pthread_mutex_unlock(&g_cfg_lock);
  {
    uint64_t _lv = __atomic_load_n(&g_last_launch_verified_at_ms, __ATOMIC_ACQUIRE);
    if (delay_ms > 0 && _lv > 0) {
      uint64_t age = now_ms() - _lv;
      if (age < (uint64_t)delay_ms) {
        cJSON_Delete(root);
        snprintf(reason, reason_size, "game still loading");
        return CHEAT_STATE_GAME_LOADING;
      }
    }
  }
  /* MC4/SHN mods without expected bytes are always BASELINE_UNKNOWN; skip the attach to avoid a double-attach race. */
  if (cheat_kind_cme != 1) {
    cJSON *mod_fp = cJSON_GetArrayItem(mods, mod_index);
    cJSON *mem_fp = cJSON_GetObjectItem(mod_fp, "memory");
    if (!cJSON_IsArray(mem_fp)) mem_fp = cJSON_GetObjectItem(mod_fp, "patches");
    int has_expected_fp = 0;
    if (cJSON_IsArray(mem_fp)) {
      cJSON *m_fp = NULL;
      cJSON_ArrayForEach(m_fp, mem_fp) {
        cJSON *ej = cJSON_GetObjectItem(m_fp, "expected");
        if (cJSON_IsString(ej) && ej->valuestring && ej->valuestring[0]) {
          has_expected_fp = 1; break;
        }
      }
    }
    if (!has_expected_fp) {
      cJSON_Delete(root);
      cheat_state_kind_t fast_st = CHEAT_STATE_BASELINE_UNKNOWN;
      snprintf(reason, reason_size, "MC4/SHN: no reliable original bytes");
      pthread_mutex_lock(&g_crash_guard_lock);
      for (int _ci = 0; _ci < g_crash_suspects_n; _ci++) {
        if (strcmp(g_crash_suspects[_ci].title_id, title_id) == 0 &&
            g_crash_suspects[_ci].mod_index == mod_index) {
          fast_st = CHEAT_STATE_CRASH_SUSPECT;
          snprintf(reason, reason_size, "Game stopped shortly after enabling this cheat.");
          break;
        }
      }
      pthread_mutex_unlock(&g_crash_guard_lock);
      return fast_st;
    }
  }
  /* read_process_memory is mdbg-backed and needs no ptrace attach. */
  if (kill(st.pid, 0) != 0 && errno == ESRCH) {
    cJSON_Delete(root);
    snprintf(reason, reason_size, "game has exited");
    return CHEAT_STATE_PROCESS_NOT_FOUND;
  }
  int is_ps2_cme = process_is_ps2_emu(st.pid);
  cheat_state_kind_t state = CHEAT_STATE_UNKNOWN;
  cJSON *mod = cJSON_GetArrayItem(mods, mod_index);
  cJSON *mem = cJSON_GetObjectItem(mod, "memory");
  if (!cJSON_IsArray(mem)) {
    mem = cJSON_GetObjectItem(mod, "patches");
  }
  if (!cJSON_IsArray(mem) || cJSON_GetArraySize(mem) == 0) {
    state = CHEAT_STATE_INVALID_CHEAT;
    snprintf(reason, reason_size, "mod has no memory entries");
  } else {
    /* Per-mod base: respect module_name, matching apply_cheat_json behaviour. */
    intptr_t mod_base_cme = st.image_base;
    {
      cJSON *mn_j_cme = cJSON_GetObjectItem(mod, "module_name");
      const char *mn_cme = (cJSON_IsString(mn_j_cme) && mn_j_cme->valuestring) ? mn_j_cme->valuestring : "";
      if (mn_cme[0] && resolve_module_base(st.pid, mn_cme, st.image_base, &mod_base_cme) != 0) {
        state = CHEAT_STATE_PROCESS_NOT_FOUND;
        snprintf(reason, reason_size, "module_not_loaded");
      }
    }
    cJSON *m = NULL;
    int on_matches = 0;
    int off_matches = 0;
    int total = 0;
    int mismatches = 0;
    int baseline_unknown_cme = 0;
    cJSON_ArrayForEach(m, mem) {
      if (state != CHEAT_STATE_UNKNOWN) break;
      cJSON *off_j = cJSON_GetObjectItem(m, "offset");
      cJSON *on_j = cJSON_GetObjectItem(m, "on");
      cJSON *off_j2 = cJSON_GetObjectItem(m, "off");
      cJSON *expected_j = cJSON_GetObjectItem(m, "expected");
      cJSON *abs_j = cJSON_GetObjectItem(m, "absolute");
      if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j) || !cJSON_IsString(off_j2)) {
        state = CHEAT_STATE_INVALID_CHEAT;
        snprintf(reason, reason_size, "memory entry missing offset/on/off");
        break;
      }
      size_t need = hex_bytes_need(on_j->valuestring);
      if (hex_bytes_need(off_j2->valuestring) > need) need = hex_bytes_need(off_j2->valuestring);
      if (cJSON_IsString(expected_j) && hex_bytes_need(expected_j->valuestring) > need) need = hex_bytes_need(expected_j->valuestring);
      uint8_t *on_b CR_AUTOFREE = malloc(need), *off_b CR_AUTOFREE = malloc(need);
      uint8_t *exp_b CR_AUTOFREE = malloc(need), *cur CR_AUTOFREE = malloc(need);
      if (!on_b || !off_b || !exp_b || !cur) {
        state = CHEAT_STATE_INVALID_CHEAT;
        snprintf(reason, reason_size, "out of memory");
        break;
      }
      size_t on_len = 0, off_len = 0, exp_len = 0;
      if (parse_hex_bytes_checked(on_j->valuestring, on_b, need, &on_len) != 0 ||
          parse_hex_bytes_checked(off_j2->valuestring, off_b, need, &off_len) != 0 || on_len != off_len) {
        state = CHEAT_STATE_INVALID_CHEAT;
        snprintf(reason, reason_size, "invalid on/off bytes");
        break;
      }
      uint64_t off_u = 0;
      if (parse_offset_hex_checked(off_j->valuestring, &off_u) != 0) {
        state = CHEAT_STATE_INVALID_CHEAT;
        snprintf(reason, reason_size, "invalid offset");
        break;
      }
      if (cJSON_IsString(expected_j) && expected_j->valuestring && expected_j->valuestring[0]) {
        if (parse_hex_bytes_checked(expected_j->valuestring, exp_b, need, &exp_len) != 0 || exp_len != on_len) {
          state = CHEAT_STATE_INVALID_CHEAT;
          snprintf(reason, reason_size, "invalid expected bytes");
          break;
        }
      }
      /* PS2 emulation: all addresses absolute (matches apply_cheat_json). */
      int af_cme = (cJSON_IsTrue(abs_j) || is_ps2_cme) ? 1 : 0;
      intptr_t addr = cheat_resolve_addr(st.pid, mod_base_cme, off_u, af_cme,
                                         on_b, exp_len > 0 ? exp_b : NULL, on_len);
      if (read_process_memory(st.pid, addr, cur, on_len) != 0) {
        state = CHEAT_STATE_READ_FAILED;
        snprintf(reason, reason_size, "read failed");
        break;
      }
      total++;
      int is_on = memcmp(cur, on_b, on_len) == 0;
      int off_reliable_cme = (cheat_kind_cme == 1) || (exp_len > 0);
      int is_off = off_reliable_cme && (memcmp(cur, (exp_len > 0 ? exp_b : off_b), on_len) == 0);
      if (is_on) { on_matches++; }
      if (is_off) { off_matches++; }
      if (!is_on && !is_off && off_reliable_cme) { mismatches++; }
      if (!is_on && !off_reliable_cme) { baseline_unknown_cme++; }
    }
    if (state == CHEAT_STATE_UNKNOWN) {
      if (on_matches == total && total > 0) {
        state = CHEAT_STATE_ON;
      } else if (mismatches > 0 && baseline_unknown_cme == 0 && on_matches == 0 && off_matches == 0) {
        state = CHEAT_STATE_MISMATCH;
        snprintf(reason, reason_size, "current bytes match neither ON nor expected/off");
      } else if (baseline_unknown_cme > 0 && mismatches == 0 && on_matches == 0) {
        state = CHEAT_STATE_BASELINE_UNKNOWN;
        snprintf(reason, reason_size, "MC4/SHN: no reliable original bytes");
      } else if (off_matches == total && total > 0 && baseline_unknown_cme == 0) {
        state = CHEAT_STATE_OFF;
      } else {
        state = CHEAT_STATE_MIXED;
      }
    }
  }
  cJSON_Delete(root);
  /* Block crash_suspect mods from being re-enabled */
  if (state != CHEAT_STATE_ON) {
    pthread_mutex_lock(&g_crash_guard_lock);
    for (int _ci = 0; _ci < g_crash_suspects_n; _ci++) {
      if (strcmp(g_crash_suspects[_ci].title_id, title_id) == 0 &&
          g_crash_suspects[_ci].mod_index == mod_index) {
        state = CHEAT_STATE_CRASH_SUSPECT;
        if (reason && reason_size > 0)
          snprintf(reason, reason_size, "Game stopped shortly after enabling this cheat.");
        break;
      }
    }
    pthread_mutex_unlock(&g_crash_guard_lock);
  }
  return state;
}

static void
classify_cheat_error_code(const char *msg, char *out, size_t out_size) {
  const char *code = "apply_failed";
  if (msg && *msg) {
    if (strstr(msg, "bytes mismatch")) {
      code = "bytes_mismatch";
    } else if (strstr(msg, "verify_failed")) {
      code = "verify_failed";
    } else if (strstr(msg, "pt_attach failed")) {
      code = "attach_failed";
    } else if (strstr(msg, "no game is currently running")) {
      code = "process_not_found";
    } else if (strstr(msg, "cheat JSON parse failed") || strstr(msg, "cheat format parse failed") ||
               strstr(msg, "no cheat file")) {
      code = "invalid_cheat";
    } else if (strstr(msg, "memory write failed")) {
      code = "write_failed";
    } else if (strstr(msg, "cheat engine disabled")) {
      code = "engine_disabled";
    } else if (strstr(msg, "app_not_stable")) {
      code = "app_not_stable";
    }
  }
  snprintf(out, out_size, "%s", code);
}

/* GET /api/cheats/apply-dryrun: shows what a real apply would do (addresses, bytes, protection) with no writes. */
void
handle_api_cheats_apply_dryrun(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char mod_s[16] = {0};
  char err[128] = {0};

  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 ||
      !title_id_normalize(raw_title_id, title_id) ||
      query_value(query, "mod", mod_s, sizeof(mod_s)) != 0) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"titleId and mod required\"}");
    return;
  }
  int mod_index = atoi(mod_s);
  if (mod_index < 0) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"mod must be >= 0\"}");
    return;
  }

  char cheat_path[256] = {0};
  int cheat_kind = 0;
  cJSON *root = load_cheat_json_root_for_title_ex(title_id, err, sizeof(err), cheat_path, sizeof(cheat_path), &cheat_kind);
  if (!root) {
    char body[256];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err);
    http_send_json(fd, 404, body);
    return;
  }

  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods) || mod_index >= cJSON_GetArraySize(mods)) {
    cJSON_Delete(root);
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"mod index out of range\"}");
    return;
  }

  running_game_state_t st;
  int running_ok = (read_running_state(&st) == 0 && st.running && strcmp(st.title_id, title_id) == 0);
  pid_t pid = running_ok ? st.pid : -1;
  intptr_t base = running_ok ? st.image_base : 0;

  /* Probing reads are mdbg-backed and need no ptrace attach. */
  int attached = 0;
  if (running_ok && pid > 0) {
    attached = (kill(pid, 0) == 0 || errno != ESRCH);
  }
  int is_ps2_dr = attached ? process_is_ps2_emu(pid) : 0;

  cJSON *out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddStringToObject(out, "titleId", title_id);
  cJSON_AddStringToObject(out, "cheatFile", cheat_path);
  cJSON_AddStringToObject(out, "format", cheat_kind == 1 ? "json" : (cheat_kind == 2 ? "shn" : "mc4"));
  {
    char ph[32]; snprintf(ph, sizeof(ph), "0x%lx", (long)base);
    cJSON_AddNumberToObject(out, "pid", (double)(running_ok ? pid : -1));
    cJSON_AddStringToObject(out, "base", ph);
    cJSON_AddBoolToObject(out, "attached", attached);
  }
  cJSON_AddNumberToObject(out, "mod", mod_index);

  cJSON *mod = cJSON_GetArrayItem(mods, mod_index);
  cJSON *mod_name_j = cJSON_GetObjectItem(mod, "name");
  cJSON_AddStringToObject(out, "modName",
    cJSON_IsString(mod_name_j) && mod_name_j->valuestring ? mod_name_j->valuestring : "");

  /* Per-mod base: respect module_name, matching apply_cheat_json behaviour. */
  intptr_t mod_base_dr = base;
  if (attached && pid > 0) {
    cJSON *mn_j_dr = cJSON_GetObjectItem(mod, "module_name");
    const char *mn_dr = (cJSON_IsString(mn_j_dr) && mn_j_dr->valuestring) ? mn_j_dr->valuestring : "";
    if (mn_dr[0]) resolve_module_base(pid, mn_dr, base, &mod_base_dr);
  }

  cJSON *memory = cJSON_GetObjectItem(mod, "memory");
  if (!cJSON_IsArray(memory)) memory = cJSON_GetObjectItem(mod, "patches");

  cJSON *writes = cJSON_AddArrayToObject(out, "writes");

  if (cJSON_IsArray(memory)) {
    int wi = 0;
    cJSON *m = NULL;
    cJSON_ArrayForEach(m, memory) {
      cJSON *off_j   = cJSON_GetObjectItem(m, "offset");
      cJSON *on_j    = cJSON_GetObjectItem(m, "on");
      cJSON *off_j2  = cJSON_GetObjectItem(m, "off");
      cJSON *exp_j   = cJSON_GetObjectItem(m, "expected");
      cJSON *abs_j   = cJSON_GetObjectItem(m, "absolute");

      cJSON *we = cJSON_CreateObject();
      cJSON_AddNumberToObject(we, "index", wi);

      if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j) || !cJSON_IsString(off_j2)) {
        cJSON_AddStringToObject(we, "error", "missing offset/on/off");
        cJSON_AddItemToArray(writes, we);
        wi++;
        continue;
      }

      size_t need = hex_bytes_need(on_j->valuestring);
      if (hex_bytes_need(off_j2->valuestring) > need) need = hex_bytes_need(off_j2->valuestring);
      if (cJSON_IsString(exp_j) && hex_bytes_need(exp_j->valuestring) > need) need = hex_bytes_need(exp_j->valuestring);
      uint8_t *on_b CR_AUTOFREE = malloc(need), *off_b CR_AUTOFREE = malloc(need);
      uint8_t *exp_b CR_AUTOFREE = malloc(need), *cur_b CR_AUTOFREE = malloc(need);
      size_t on_len = 0, off_len = 0, exp_len = 0;
      uint64_t off_u = 0;

      if (!on_b || !off_b || !exp_b || !cur_b ||
          parse_hex_bytes_checked(on_j->valuestring, on_b, need, &on_len) != 0 ||
          parse_hex_bytes_checked(off_j2->valuestring, off_b, need, &off_len) != 0 ||
          parse_offset_hex_checked(off_j->valuestring, &off_u) != 0 ||
          on_len == 0 || on_len != off_len) {
        cJSON_AddStringToObject(we, "error", "hex parse failed or on/off size mismatch");
        cJSON_AddItemToArray(writes, we);
        wi++;
        continue;
      }
      if (cJSON_IsString(exp_j) && exp_j->valuestring && exp_j->valuestring[0]) {
        parse_hex_bytes_checked(exp_j->valuestring, exp_b, need, &exp_len);
      }

      cJSON_AddStringToObject(we, "rawOffset", off_j->valuestring);
      cJSON_AddStringToObject(we, "onBytes", on_j->valuestring);
      cJSON_AddStringToObject(we, "offBytes", off_j2->valuestring);
      cJSON_AddNumberToObject(we, "len", (double)on_len);

      /* PS2 emulation: all addresses absolute (matches apply_cheat_json). */
      int af = (cJSON_IsTrue(abs_j) || is_ps2_dr) ? 1 : 0;
      intptr_t addr = (pid > 0 && attached) ?
        cheat_resolve_addr(pid, mod_base_dr, off_u, af, on_b, exp_len > 0 ? exp_b : NULL, on_len) :
        (af ? (intptr_t)off_u : mod_base_dr + (intptr_t)off_u);

      char addrh[32]; snprintf(addrh, sizeof(addrh), "0x%lx", (long)addr);
      cJSON_AddStringToObject(we, "addr", addrh);

      intptr_t page = (intptr_t)ROUND_PG_DOWN((uintptr_t)addr);
      size_t span = (size_t)(ROUND_PG_UP((uintptr_t)addr + on_len) - (uintptr_t)page);
      char pageh[32]; snprintf(pageh, sizeof(pageh), "0x%lx", (long)page);
      cJSON_AddStringToObject(we, "page", pageh);
      snprintf(pageh, sizeof(pageh), "0x%zx", span);
      cJSON_AddStringToObject(we, "span", pageh);

      if (attached && pid > 0) {
        int prot = kernel_get_vmem_protection(pid, page, span);
        cJSON_AddNumberToObject(we, "vmProt", (double)prot);

        int rdok = (read_process_memory(pid, addr, cur_b, on_len) == 0);
        cJSON_AddBoolToObject(we, "readOk", rdok);
        if (rdok) {
          char cur_hex[49] = {0};
          fmt_hex16(cur_b, on_len, cur_hex, sizeof(cur_hex));
          cJSON_AddStringToObject(we, "currentBytes", cur_hex);
          int is_on  = (memcmp(cur_b, on_b,  on_len) == 0);
          int is_off = (exp_len > 0) ? (memcmp(cur_b, exp_b, on_len) == 0) :
                                       (memcmp(cur_b, off_b, on_len) == 0);
          cJSON_AddBoolToObject(we, "currentIsOn",  is_on);
          cJSON_AddBoolToObject(we, "currentIsOff", is_off);
        }
      } else {
        cJSON_AddBoolToObject(we, "readOk", 0);
        cJSON_AddStringToObject(we, "currentBytes", "");
      }

      cJSON_AddItemToArray(writes, we);
      wi++;
    }
  }

  cJSON_Delete(root);

  char *body = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (body) {
    http_send_json(fd, 200, body);
    free(body);
  } else {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"json encode failed\"}");
  }
}

void
handle_api_cheats_toggle(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char idx_s[16] = {0};
  char on_s[16] = {0};
  char body[512];
  char err[256] = {0};

  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id) ||
      query_value(query, "index", idx_s, sizeof(idx_s)) != 0 || query_value(query, "on", on_s, sizeof(on_s)) != 0) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing args\"}");
    return;
  }

  int idx = atoi(idx_s);
  int on = strcmp(on_s, "0") != 0;
  int force = 0;
  char force_s[8] = {0};
  if (query_value(query, "force", force_s, sizeof(force_s)) == 0 && atoi(force_s) != 0) {
    force = 1;
  }
  if (idx < 0) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad index\"}");
    return;
  }
  int allow_force = 0;
  pthread_mutex_lock(&g_cfg_lock);
  allow_force = g_cfg.allow_force_enable;
  pthread_mutex_unlock(&g_cfg_lock);
  if (on && !(force && allow_force)) {
    char reason[160] = {0};
    cheat_state_kind_t st = check_mod_enable_state(title_id, idx, reason, sizeof(reason));
    /* Only a crash-suspect stops a click; everything else is left to the apply itself. */
    int block = (st == CHEAT_STATE_CRASH_SUSPECT);
    if (block) {
      char *esc = json_escape(reason[0] ? reason : "refusing to patch");
      snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\",\"message\":\"%s\"}",
               cheat_state_key(st), esc ? esc : "refusing to patch");
      free(esc);
      cr_log("warn", "cheats", "refusing enable mod=%d title=%s reason=%s", idx, title_id, reason);
      http_send_json(fd, 400, body);
      return;
    }
  }

  int rc = apply_cheat_json(title_id, idx, on, err, sizeof(err));
  if (rc == -2) {
    http_send_json(fd, 429,
      "{\"ok\":false,\"error\":\"apply_in_progress\","
      "\"message\":\"Another cheat is being applied or monitored. Wait for the safety window to finish.\"}");
    return;
  }
  if (rc != 0) {
    char *esc = json_escape(err[0] ? err : "apply failed");
    char code[64];
    running_game_state_t st;
    running_state_get(&st);
    classify_cheat_error_code(err, code, sizeof(code));
    if (!strcmp(code, "app_not_stable")) {
      char *msg_esc = json_escape("Game is not stable enough for patching yet. Wait a few seconds and try again.");
      snprintf(body, sizeof(body),
               "{\"ok\":false,\"error\":\"app_not_stable\",\"message\":\"%s\"}",
               msg_esc ? msg_esc : "Game not stable");
      free(msg_esc);
      free(esc);
      http_send_json(fd, 400, body);
      return;
    }
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", esc ? esc : "apply failed");
    free(esc);
    notification_add("cheat_error", "Cheat apply failed: %s idx=%d (%s)", title_id, idx, err);
    cr_log("error", "cheats", "apply failed %s idx=%d: %s", title_id, idx, err);
    http_send_json(fd, 400, body);
    return;
  }
  snprintf(body, sizeof(body), "{\"ok\":true,\"enabled\":%s}", on ? "true" : "false");
  http_send_json(fd, 200, body);
}

static int
cheats_delete_from_dir(const char *dir, const char *title_id) {
  int removed = 0;
  DIR *d = opendir(dir);
  if (!d) return 0;
  struct dirent *ent = NULL;
  while ((ent = readdir(d)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.' || !is_safe_filename(name) || !recognised_cheat_extension(name)) continue;
    char id[10];
    if (!extract_title_id_prefix(name, id, sizeof(id))) continue;
    if (strcmp(id, title_id) != 0) continue;
    char path[768];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (unlink(path) == 0 || errno == ENOENT) removed++;
  }
  closedir(d);
  return removed;
}

void
handle_api_cheats_delete(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};

  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }

  int removed = 0;
  /* Delete from top-level and all format subdirectories */
  removed += cheats_delete_from_dir(CHEATRUNNER_CHEATS_DIR,      title_id);
  removed += cheats_delete_from_dir(CHEATRUNNER_CHEATS_JSON_DIR, title_id);
  removed += cheats_delete_from_dir(CHEATRUNNER_CHEATS_SHN_DIR,  title_id);
  removed += cheats_delete_from_dir(CHEATRUNNER_CHEATS_MC4_DIR,  title_id);

  char body[128];
  snprintf(body, sizeof(body), "{\"ok\":true,\"removed\":%d}", removed);
  http_send_json(fd, 200, body);
}

void
handle_api_cheats_engine(int fd) {
  char body[64];
  pthread_mutex_lock(&g_cfg_lock);
  snprintf(body, sizeof(body), "{\"ok\":true,\"enabled\":%s}", g_cfg.cheat_engine ? "true" : "false");
  pthread_mutex_unlock(&g_cfg_lock);
  http_send_json(fd, 200, body);
}

void
handle_api_cheats_engine_toggle(int fd, const char *query) {
  char on_s[16] = {0};
  int on = 0;
  if (query_value(query, "on", on_s, sizeof(on_s)) != 0) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing on\"}");
    return;
  }
  on = atoi(on_s) ? 1 : 0;
  pthread_mutex_lock(&g_cfg_lock);
  g_cfg.cheat_engine = on;
  int rc = config_save_locked();
  pthread_mutex_unlock(&g_cfg_lock);
  if (rc != 0) {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"save failed\"}");
    return;
  }
  notification_add("cheat_engine", "Cheat engine %s", on ? "enabled" : "disabled");
  cr_log("info", "cheats", "cheat engine %s", on ? "enabled" : "disabled");
  http_send_json(fd, 200, on ? "{\"ok\":true,\"enabled\":true}" : "{\"ok\":true,\"enabled\":false}");
}

void
handle_api_cheats_raw(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char path[256];
  int kind = 0;
  char *txt = NULL;
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  if (!find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) {
    http_send_json(fd, 404, "{\"ok\":false,\"error\":\"no cheat file\"}");
    return;
  }
  if (read_file_text(path, &txt) != 0 || !txt) {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"read failed\"}");
    return;
  }
  http_send_response(fd, 200, "text/plain; charset=utf-8", (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
handle_api_cheats_validate(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  char err[128] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  cJSON *root = load_cheat_json_root_for_title(title_id, err, sizeof(err));
  if (!root) {
    char body[256];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err);
    http_send_json(fd, 400, body);
    return;
  }
  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods)) {
    cJSON_Delete(root);
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"mods missing\"}");
    return;
  }
  int invalid = 0;
  cJSON *m = NULL;
  cJSON_ArrayForEach(m, mods) {
    cJSON *mem = cJSON_GetObjectItem(m, "memory");
    if (!cJSON_IsArray(mem)) {
      mem = cJSON_GetObjectItem(m, "patches");
    }
    if (!cJSON_IsArray(mem) || cJSON_GetArraySize(mem) == 0) {
      invalid++;
      continue;
    }
    cJSON *w = NULL;
    cJSON_ArrayForEach(w, mem) {
      cJSON *off_j = cJSON_GetObjectItem(w, "offset");
      cJSON *on_j = cJSON_GetObjectItem(w, "on");
      cJSON *off_j2 = cJSON_GetObjectItem(w, "off");
      if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j) || !cJSON_IsString(off_j2)) {
        invalid++;
        continue;
      }
      size_t need = hex_bytes_need(on_j->valuestring);
      if (hex_bytes_need(off_j2->valuestring) > need) need = hex_bytes_need(off_j2->valuestring);
      uint8_t *b1 CR_AUTOFREE = malloc(need), *b2 CR_AUTOFREE = malloc(need);
      size_t l1 = 0, l2 = 0;
      uint64_t off_u = 0;
      if (!b1 || !b2 || parse_offset_hex_checked(off_j->valuestring, &off_u) != 0 ||
          parse_hex_bytes_checked(on_j->valuestring, b1, need, &l1) != 0 ||
          parse_hex_bytes_checked(off_j2->valuestring, b2, need, &l2) != 0 || l1 != l2) {
        invalid++;
      }
    }
  }
  cJSON_Delete(root);
  char body[256];
  snprintf(body, sizeof(body), "{\"ok\":%s,\"titleId\":\"%s\",\"invalidCount\":%d}", invalid ? "false" : "true", title_id, invalid);
  http_send_json(fd, invalid ? 400 : 200, body);
}

void
handle_api_cheats_upload(int fd, const char *query, const char *body, size_t body_len) {
  char filename[128] = {0};
  if (query_value(query, "filename", filename, sizeof(filename)) != 0 || !filename[0]) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing filename\"}");
    return;
  }
  if (!is_safe_filename(filename)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"unsafe filename\"}");
    return;
  }
  const char *ext = strrchr(filename, '.');
  if (!ext) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"no extension\"}");
    return;
  }
  const char *dir = NULL;
  size_t max_size = 0;
  if (strcasecmp(ext, ".json") == 0) {
    dir = CHEATRUNNER_CHEATS_JSON_DIR;
    max_size = 2 * 1024 * 1024;
  } else if (strcasecmp(ext, ".shn") == 0) {
    dir = CHEATRUNNER_CHEATS_SHN_DIR;
    max_size = 1024 * 1024;
  } else if (strcasecmp(ext, ".mc4") == 0) {
    dir = CHEATRUNNER_CHEATS_MC4_DIR;
    max_size = 1024 * 1024;
  } else {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"unsupported extension\"}");
    return;
  }
  if (body_len == 0) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }
  if (body_len > max_size) {
    char resp[192];
    snprintf(resp, sizeof(resp),
             "{\"ok\":false,\"error\":\"payload_too_large\",\"message\":\"Upload exceeds maximum allowed size for this format.\",\"maxBytes\":%u}",
             (unsigned int)max_size);
    http_send_json(fd, 413, resp);
    return;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir, filename);
  if (write_file_atomic(path, (const uint8_t *)body, body_len) != 0) {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"write failed\"}");
    return;
  }
  char resp[256];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"filename\":\"%s\"}", filename);
  http_send_json(fd, 200, resp);
}

/* Shows which of the absolute/relative addresses matches ON/OFF/expected; needs a running game. */
void
handle_api_cheats_address_debug(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }

  char cheat_path[256] = {0};
  char err[128] = {0};
  int cheat_kind = 0;
  cJSON *root = load_cheat_json_root_for_title_ex(title_id, err, sizeof(err), cheat_path, sizeof(cheat_path), &cheat_kind);
  if (!root) {
    char body[256];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err);
    http_send_json(fd, 404, body);
    return;
  }

  running_game_state_t st;
  int running_ok = (read_running_state(&st) == 0 && st.running);
  int title_match = running_ok && strcmp(st.title_id, title_id) == 0;
  pid_t pid = running_ok ? st.pid : -1;
  intptr_t base = running_ok ? st.image_base : 0;

  /* Probing reads are mdbg-backed and need no ptrace attach. */
  int attach_ok = 0;
  if (title_match && pid > 0 && base != 0) {
    attach_ok = (kill(pid, 0) == 0 || errno != ESRCH);
  }

  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods)) { mods = cJSON_GetObjectItem(root, "patches"); }

  cJSON *out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddStringToObject(out, "titleId", title_id);
  cJSON_AddStringToObject(out, "cheatPath", cheat_path);
  const char *fmt_s = cheat_kind == 1 ? "json" : (cheat_kind == 2 ? "shn" : "mc4");
  cJSON_AddStringToObject(out, "cheatFormat", fmt_s);
  {
    cJSON *g = cJSON_AddObjectToObject(out, "game");
    if (g) {
      cJSON_AddBoolToObject(g, "running", running_ok);
      cJSON_AddBoolToObject(g, "titleMatch", title_match);
      cJSON_AddBoolToObject(g, "attachOk", attach_ok);
      char bh[32]; snprintf(bh, sizeof(bh), "0x%lx", (long)base);
      cJSON_AddStringToObject(g, "base", bh);
      cJSON_AddNumberToObject(g, "pid", (double)(running_ok ? pid : -1));
    }
  }

  cJSON *mod_arr = cJSON_AddArrayToObject(out, "mods");

  if (cJSON_IsArray(mods)) {
    cJSON *mod = NULL;
    cJSON_ArrayForEach(mod, mods) {
      cJSON *mod_obj = cJSON_CreateObject();
      cJSON *n = cJSON_GetObjectItem(mod, "name");
      cJSON_AddStringToObject(mod_obj, "name", cJSON_IsString(n) ? n->valuestring : "");

      cJSON *mem = cJSON_GetObjectItem(mod, "memory");
      if (!cJSON_IsArray(mem)) mem = cJSON_GetObjectItem(mod, "patches");

      cJSON *patch_arr = cJSON_AddArrayToObject(mod_obj, "patches");

      if (cJSON_IsArray(mem)) {
        int wi = 0;
        cJSON *m = NULL;
        cJSON_ArrayForEach(m, mem) {
          cJSON *off_j = cJSON_GetObjectItem(m, "offset");
          cJSON *on_j  = cJSON_GetObjectItem(m, "on");
          cJSON *off_j2= cJSON_GetObjectItem(m, "off");
          cJSON *exp_j = cJSON_GetObjectItem(m, "expected");
          cJSON *abs_j = cJSON_GetObjectItem(m, "absolute");

          cJSON *pe = cJSON_CreateObject();
          cJSON_AddNumberToObject(pe, "index", wi++);

          if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j) || !cJSON_IsString(off_j2)) {
            cJSON_AddStringToObject(pe, "error", "missing offset/on/off");
            cJSON_AddItemToArray(patch_arr, pe);
            continue;
          }

          size_t need = hex_bytes_need(on_j->valuestring);
          if (hex_bytes_need(off_j2->valuestring) > need) need = hex_bytes_need(off_j2->valuestring);
          if (cJSON_IsString(exp_j) && hex_bytes_need(exp_j->valuestring) > need) need = hex_bytes_need(exp_j->valuestring);
          uint8_t *on_b CR_AUTOFREE = malloc(need), *off_b CR_AUTOFREE = malloc(need), *exp_b CR_AUTOFREE = malloc(need);
          size_t on_len = 0, off_len = 0, exp_len = 0;
          uint64_t off_u = 0;

          if (!on_b || !off_b || !exp_b ||
              parse_hex_bytes_checked(on_j->valuestring, on_b, need, &on_len) != 0 ||
              parse_hex_bytes_checked(off_j2->valuestring, off_b, need, &off_len) != 0 ||
              on_len != off_len || on_len == 0) {
            cJSON_AddStringToObject(pe, "error", "invalid on/off bytes");
            cJSON_AddItemToArray(patch_arr, pe);
            continue;
          }
          if (parse_offset_hex_checked(off_j->valuestring, &off_u) != 0) {
            cJSON_AddStringToObject(pe, "error", "invalid offset");
            cJSON_AddItemToArray(patch_arr, pe);
            continue;
          }
          if (cJSON_IsString(exp_j) && exp_j->valuestring && exp_j->valuestring[0]) {
            parse_hex_bytes_checked(exp_j->valuestring, exp_b, need, &exp_len);
          }

          char off_hex[32]; snprintf(off_hex, sizeof(off_hex), "0x%llX", (unsigned long long)off_u);
          cJSON_AddStringToObject(pe, "offset", off_hex);
          cJSON_AddBoolToObject(pe, "absoluteFlag", cJSON_IsTrue(abs_j));

          intptr_t abs_cand = (intptr_t)off_u;
          intptr_t rel_cand = base + (intptr_t)off_u;

          const uint8_t *expect_cmp = exp_len > 0 ? exp_b : NULL;
          intptr_t resolved = cheat_resolve_addr(pid, base, off_u, cJSON_IsTrue(abs_j) ? 1 : 0,
                                                 on_b, expect_cmp, on_len);
          char res_hex[32]; snprintf(res_hex, sizeof(res_hex), "0x%lx", (long)resolved);
          cJSON_AddStringToObject(pe, "resolvedAddress", res_hex);
          cJSON_AddStringToObject(pe, "resolvedMode",
                                  resolved == abs_cand ? "absolute" : "relative");

          cJSON *cands_j = cJSON_AddArrayToObject(pe, "candidates");

          /* absolute candidate */
          {
            cJSON *ca = cJSON_CreateObject();
            char ah[32]; snprintf(ah, sizeof(ah), "0x%lx", (long)abs_cand);
            cJSON_AddStringToObject(ca, "mode", "absolute");
            cJSON_AddStringToObject(ca, "address", ah);
            uint8_t *buf CR_AUTOFREE = attach_ok ? malloc(on_len) : NULL;
            if (buf) {
              int rd = read_process_memory(pid, abs_cand, buf, on_len);
              cJSON_AddBoolToObject(ca, "readOk", rd == 0);
              if (rd == 0) {
                char hx[512]; bytes_to_hex(buf, on_len, hx, sizeof(hx));
                cJSON_AddStringToObject(ca, "current", hx);
                cJSON_AddBoolToObject(ca, "matchOn",  memcmp(buf, on_b, on_len) == 0);
                cJSON_AddBoolToObject(ca, "matchOff",
                  expect_cmp ? memcmp(buf, expect_cmp, on_len) == 0 : 0);
              }
            } else {
              cJSON_AddBoolToObject(ca, "readOk", 0);
            }
            cJSON_AddItemToArray(cands_j, ca);
          }

          /* relative candidate */
          {
            cJSON *cr2 = cJSON_CreateObject();
            char rh[32]; snprintf(rh, sizeof(rh), "0x%lx", (long)rel_cand);
            cJSON_AddStringToObject(cr2, "mode", "relative");
            cJSON_AddStringToObject(cr2, "address", rh);
            uint8_t *buf CR_AUTOFREE = attach_ok ? malloc(on_len) : NULL;
            if (buf) {
              int rd = read_process_memory(pid, rel_cand, buf, on_len);
              cJSON_AddBoolToObject(cr2, "readOk", rd == 0);
              if (rd == 0) {
                char hx[512]; bytes_to_hex(buf, on_len, hx, sizeof(hx));
                cJSON_AddStringToObject(cr2, "current", hx);
                cJSON_AddBoolToObject(cr2, "matchOn",  memcmp(buf, on_b, on_len) == 0);
                cJSON_AddBoolToObject(cr2, "matchOff",
                  expect_cmp ? memcmp(buf, expect_cmp, on_len) == 0 : 0);
              }
            } else {
              cJSON_AddBoolToObject(cr2, "readOk", 0);
            }
            cJSON_AddItemToArray(cands_j, cr2);
          }

          char on_hex_s[512], off_hex_s[512];
          bytes_to_hex(on_b, on_len, on_hex_s, sizeof(on_hex_s));
          bytes_to_hex(off_b, on_len, off_hex_s, sizeof(off_hex_s));
          cJSON_AddStringToObject(pe, "on",  on_hex_s);
          cJSON_AddStringToObject(pe, "valueOff", off_hex_s);
          if (exp_len > 0) {
            char exp_hex_s[512]; bytes_to_hex(exp_b, on_len, exp_hex_s, sizeof(exp_hex_s));
            cJSON_AddStringToObject(pe, "expected", exp_hex_s);
          } else {
            cJSON_AddNullToObject(pe, "expected");
          }
          cJSON_AddNumberToObject(pe, "byteLen", (double)on_len);

          cJSON_AddItemToArray(patch_arr, pe);
        }
      }
      cJSON_AddItemToArray(mod_arr, mod_obj);
    }
  }

  cJSON_Delete(root);

  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}


void
handle_api_cheats_find(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 ||
      !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid titleId\"}");
    return;
  }

  cheat_file_search_t ctx;
  int found_local = find_cheat_candidates(title_id, &ctx);

  char cands_buf[2048] = "[";
  int cpos = 1;
  for (int ci = 0; ci < ctx.candidate_count && ci < MAX_CHEAT_CANDIDATES; ci++) {
    const char *cf = ctx.candidates[ci].kind == 1 ? "json" : (ctx.candidates[ci].kind == 2 ? "shn" : "mc4");
    const char *cb = strrchr(ctx.candidates[ci].path, '/');
    cb = cb ? cb + 1 : ctx.candidates[ci].path;
    char entry[512];
    int en = snprintf(entry, sizeof(entry), "%s{\"format\":\"%s\",\"filename\":\"%s\",\"path\":\"%s\",\"score\":%d}",
                      ci > 0 ? "," : "", cf, cb, ctx.candidates[ci].path, ctx.candidates[ci].score);
    if (en > 0 && cpos + en + 2 < (int)sizeof(cands_buf)) {
      memcpy(cands_buf + cpos, entry, en);
      cpos += en;
    }
  }
  cands_buf[cpos++] = ']';
  cands_buf[cpos]   = '\0';

  if (found_local) {
    const char *sel_fmt = ctx.best_kind == 1 ? "json" : (ctx.best_kind == 2 ? "shn" : "mc4");
    const char *sel_base = strrchr(ctx.best_path, '/');
    sel_base = sel_base ? sel_base + 1 : ctx.best_path;
    char body[4096];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"local\":true,\"source\":\"local\",\"titleId\":\"%s\","
             "\"format\":\"%s\",\"filename\":\"%s\",\"path\":\"%s\","
             "\"selectedReason\":\"highest score local file\","
             "\"candidateCount\":%d,\"candidates\":%s}",
             title_id, sel_fmt, sel_base, ctx.best_path,
             ctx.candidate_count, cands_buf);
    http_send_json(fd, 200, body);
    return;
  }

  char body[512];
  snprintf(body, sizeof(body),
           "{\"ok\":false,\"error\":\"no_local_cheat_found\","
           "\"message\":\"No local cheat file found in /data/cheatrunner/cheats.\","
           "\"candidateCount\":%d,\"candidates\":%s}",
           ctx.candidate_count, cands_buf);
  http_send_json(fd, 404, body);
}

void
handle_api_cheats_download(int fd, const char *query) {
  (void)query;
  http_send_json(fd, 410,
    "{\"ok\":false,\"error\":\"remote_download_removed\","
    "\"message\":\"Remote cheat download was removed. Use local cheat files in /data/cheatrunner/cheats.\"}");
}

/* Scan a single subdirectory of CHEATRUNNER_CHEATS_DIR and append JSON file objects. */
static void
cheats_list_scan_subdir(const char *fmt_name, int kind, cJSON *arr) {
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/%s", CHEATRUNNER_CHEATS_DIR, fmt_name);
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *ent = NULL;
  while ((ent = readdir(d)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') continue;
    if (!is_safe_filename(name) || !recognised_cheat_extension(name)) continue;
    char full[768];
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    cJSON *e = cJSON_CreateObject();
    if (!e) continue;
    char rel[512];
    snprintf(rel, sizeof(rel), "%s/%s", fmt_name, name);
    char title_id[10] = {0};
    extract_title_id_prefix(name, title_id, sizeof(title_id));
    cJSON_AddStringToObject(e, "path", rel);
    cJSON_AddStringToObject(e, "format", fmt_name);
    cJSON_AddNumberToObject(e, "kind", kind);
    cJSON_AddStringToObject(e, "titleId", title_id);
    cJSON_AddItemToArray(arr, e);
  }
  closedir(d);
}

void
handle_api_cheats_list(int fd) {
  cJSON *out = cJSON_CreateObject();
  if (!out) { http_send_oom(fd); return; }
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON *arr = cJSON_AddArrayToObject(out, "files");

  /* Scan recognised format subdirectories */
  cheats_list_scan_subdir("json", 1, arr);
  cheats_list_scan_subdir("shn",  2, arr);
  cheats_list_scan_subdir("mc4",  3, arr);

  /* Also scan the top-level cheats dir for files placed directly there */
  DIR *d = opendir(CHEATRUNNER_CHEATS_DIR);
  if (d) {
    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
      const char *name = ent->d_name;
      if (name[0] == '.') continue;
      if (!is_safe_filename(name) || !recognised_cheat_extension(name)) continue;
      char full[768];
      snprintf(full, sizeof(full), "%s/%s", CHEATRUNNER_CHEATS_DIR, name);
      struct stat st;
      if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
      cJSON *e = cJSON_CreateObject();
      if (!e) continue;
      char title_id[10] = {0};
      extract_title_id_prefix(name, title_id, sizeof(title_id));
      /* Determine format from extension */
      const char *fmt = "json";
      int kind = 1;
      size_t nl = strlen(name);
      if (nl > 4 && strcasecmp(name + nl - 4, ".mc4") == 0) { fmt = "mc4"; kind = 3; }
      else if (nl > 4 && strcasecmp(name + nl - 4, ".shn") == 0) { fmt = "shn"; kind = 2; }
      cJSON_AddStringToObject(e, "path", name);
      cJSON_AddStringToObject(e, "format", fmt);
      cJSON_AddNumberToObject(e, "kind", kind);
      cJSON_AddStringToObject(e, "titleId", title_id);
      cJSON_AddItemToArray(arr, e);
    }
    closedir(d);
  }

  char *txt = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!txt) { http_send_oom(fd); return; }
  http_send_json(fd, 200, txt);
  free(txt);
}

/* POST /api/cheats/repo/download?source=<name|all>[&overwrite=1] */
void
handle_api_cheats_repo_download(int fd, const char *query) {
  char source[32] = {0};
  char ovr_s[4]   = {0};
  query_value(query, "source",    source, sizeof(source));
  query_value(query, "overwrite", ovr_s,  sizeof(ovr_s));
  int overwrite = (ovr_s[0] == '1') ? 1 : 0;

  if (!source[0] || (strcmp(source, "hencollection") != 0 &&
                     strcmp(source, "ps5cheats")     != 0 &&
                     strcmp(source, "goldhen")       != 0 &&
                     strcmp(source, "henppsa")       != 0 &&
                     strcmp(source, "psgamepatch")   != 0 &&
                     strcmp(source, "crpatches")     != 0 &&
                     strcmp(source, "all")           != 0)) {
    http_send_json(fd, 400,
      "{\"ok\":false,\"error\":\"invalid_source\","
      "\"message\":\"source must be hencollection, ps5cheats, goldhen, henppsa, psgamepatch, crpatches, or all\"}");
    return;
  }

  if (repo_mirror_start(source, overwrite) != 0) {
    char buf[4096];
    repo_mirror_status_json(buf, sizeof(buf));
    http_send_json(fd, 409, buf);
    return;
  }

  char buf[512];
  snprintf(buf, sizeof(buf),
    "{\"ok\":true,\"state\":\"running\",\"source\":\"%s\",\"overwrite\":%d}", source, overwrite);
  http_send_json(fd, 200, buf);
}

/* GET /api/cheats/repo/download/status */
void
handle_api_cheats_repo_download_status(int fd) {
  char buf[4096];
  repo_mirror_status_json(buf, sizeof(buf));
  http_send_json(fd, 200, buf);
}

/* Starts a mirror of all sources (older endpoint, kept for compatibility). */
void
handle_api_cheats_download_all(int fd) {
  if (repo_mirror_start("all", 0) == 0) {
    http_send_json(fd, 200, "{\"ok\":true,\"state\":\"running\",\"source\":\"all\",\"overwrite\":0}");
  } else {
    /* Already running or other conflict — return current status */
    handle_api_cheats_repo_download_status(fd);
  }
}

void
handle_api_cheats_download_all_status(int fd) {
  handle_api_cheats_repo_download_status(fd);
}

/* ── /api/dev/privileges ────────────────────────────────────────── */
void
handle_api_dev_privileges(int fd) {
  const cr_priv_status_t *ps = cr_priv_get();
  cJSON *out = cJSON_CreateObject();
  if (!out) { http_send_oom(fd); return; }

  cJSON_AddBoolToObject(out, "ok", ps->can_patch_game ? 1 : 0);
  cJSON_AddNumberToObject(out, "uid", ps->uid);
  cJSON_AddNumberToObject(out, "gid", ps->gid);
  cJSON_AddNumberToObject(out, "euid", ps->euid);
  cJSON_AddNumberToObject(out, "egid", ps->egid);
  cJSON_AddBoolToObject(out, "uidOk", ps->uid_ok);
  cJSON_AddBoolToObject(out, "sandboxEscaped", ps->sandbox_ok);
  cJSON_AddBoolToObject(out, "rootDirOk", ps->rootvnode_ok);
  cJSON_AddBoolToObject(out, "sceAuthOk", ps->authid_ok);
  cJSON_AddBoolToObject(out, "sceCapsOk", ps->caps_ok);
  cJSON_AddBoolToObject(out, "sceAttrsOk", ps->attrs_ok);
  cJSON_AddBoolToObject(out, "kernelRwOk", ps->kernel_rw_ok);
  cJSON_AddBoolToObject(out, "kernelMprotectOk", ps->kernel_mprotect_ok);
  cJSON_AddBoolToObject(out, "gameMemoryPatchOk", ps->can_patch_game);
  cJSON_AddBoolToObject(out, "canPatchGameMemory", ps->can_patch_game);

  char authid_hex[24];
  snprintf(authid_hex, sizeof(authid_hex), "0x%llx", (unsigned long long)ps->authid);
  cJSON_AddStringToObject(out, "authId", authid_hex);

  cJSON *warn_arr = cJSON_AddArrayToObject(out, "warnings");
  if (warn_arr) {
    for (int i = 0; i < ps->n_warnings; i++) {
      cJSON *ws = cJSON_CreateString(ps->warnings[i]);
      if (ws) cJSON_AddItemToArray(warn_arr, ws);
    }
  }

  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}

/* ── /api/dev/diag ──────────────────────────────────────────────── */
void
handle_api_dev_diag(int fd) {
  cJSON *out = cJSON_CreateObject();
  if (!out) { http_send_oom(fd); return; }
  cJSON_AddBoolToObject(out, "ok", 1);

  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}

void
handle_api_dev_open_browser(int fd, const char *method, const char *query) {
  if (strcmp(method, "POST") != 0) {
    http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\",\"message\":\"Use POST.\"}");
    return;
  }

  int dev_enabled = 0;
  pthread_mutex_lock(&g_cfg_lock);
  dev_enabled = g_cfg.dev_reload_enabled;
  pthread_mutex_unlock(&g_cfg_lock);
  if (!dev_enabled) {
    http_send_json(fd, 403, "{\"ok\":false,\"error\":\"dev_reload_disabled\"}");
    return;
  }

  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  query_value(query, "titleId", raw_title_id, sizeof(raw_title_id));
  if (raw_title_id[0] && !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid_titleId\"}");
    return;
  }
  if (!title_id[0]) {
    running_game_state_t st;
    memset(&st, 0, sizeof(st));
    if (read_running_state(&st) == 0 && st.running && st.title_id[0]) {
      snprintf(title_id, sizeof(title_id), "%s", st.title_id);
    }
  }

  char url[192];
  if (title_id[0]) {
    snprintf(url, sizeof(url), "http://127.0.0.1:9999/#trainer=%s", title_id);
  } else {
    snprintf(url, sizeof(url), "http://127.0.0.1:9999");
  }

#if CHEATRUNNER_HAVE_BROWSER_OPEN
  int rc = cr_browser_open_url(url);
  char body[512];
  snprintf(body, sizeof(body), "{\"ok\":true,\"url\":\"%s\",\"titleId\":\"%s\",\"rc\":%d}",
           url, title_id, rc);
  http_send_json(fd, 200, body);
#else
  (void)url;
  (void)title_id;
  http_send_json(fd, 501, "{\"ok\":false,\"error\":\"browser_open_not_compiled\",\"message\":\"Browser test endpoint is unavailable in this build.\"}");
#endif
}

/* ── /api/dev/memtest ───────────────────────────────────────────── */
void
handle_api_dev_memtest(int fd) {
  cJSON *out = cJSON_CreateObject();
  if (!out) { http_send_oom(fd); return; }

  cJSON_AddBoolToObject(out, "ok", 1);

  /* Level 1: self-process buffer test */
  static const uint8_t PATTERN[8] = {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE};
  uint8_t *buf = (uint8_t *)mmap(NULL, 0x4000, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  int self_ok = 0;
  char self_err[64] = "mmap failed";
  if (buf != MAP_FAILED) {
    memset(buf, 0, 8);
    memcpy(buf, PATTERN, sizeof(PATTERN));
    if (memcmp(buf, PATTERN, sizeof(PATTERN)) == 0) {
      memset(buf, 0, 8);
      self_ok = 1;
      self_err[0] = '\0';
    } else {
      snprintf(self_err, sizeof(self_err), "readback mismatch");
    }
    munmap(buf, 0x4000);
  }

  cJSON *self = cJSON_AddObjectToObject(out, "selfTest");
  if (self) {
    cJSON_AddBoolToObject(self, "ok", self_ok);
    if (!self_ok) cJSON_AddStringToObject(self, "error", self_err);
  }

  /* Level 2: running game process dry test (read + mprotect probe, no writes) */
  running_game_state_t rgs;
  running_state_get(&rgs);
  cJSON *game = cJSON_AddObjectToObject(out, "gameProcessTest");
  if (game) {
    if (!rgs.running || rgs.pid <= 0) {
      cJSON_AddBoolToObject(game, "skipped", 1);
      cJSON_AddStringToObject(game, "reason", "no game running");
    } else {
      cJSON_AddBoolToObject(game, "skipped", 0);
      cJSON_AddNumberToObject(game, "pid", (double)rgs.pid);
      char base_hex[24];
      snprintf(base_hex, sizeof(base_hex), "0x%lx", (long)rgs.image_base);
      cJSON_AddStringToObject(game, "imageBase", base_hex);

      /* Probe read at image base — requires ptrace attach first */
      uint8_t probe[8] = {0};
      int pt_rc = -1;
      int mprotect_rc = -1;
      int attach_rc = -1;
      if (rgs.image_base) {
        intptr_t pg = (intptr_t)ROUND_PG_DOWN((uintptr_t)rgs.image_base);
        size_t sp = (size_t)0x4000;
        int orig_prot = kernel_get_vmem_protection(rgs.pid, pg, sp);
        /* Read-only probe: never set W+X here, PS5's hypervisor enforces W^X and RWX can panic the kernel. */
        mprotect_rc = kernel_mprotect(rgs.pid, pg, sp, PROT_READ | PROT_EXEC);
        if (mprotect_rc == 0) {
          attach_rc = pt_attach_timed(rgs.pid, 2000);
          if (attach_rc == 0) {
            pt_rc = pt_copyout(rgs.pid, rgs.image_base, probe, sizeof(probe));
            pt_detach(rgs.pid, 0);
          }
          int rp = orig_prot >= 0 ? orig_prot : (PROT_READ | PROT_EXEC);
          kernel_mprotect(rgs.pid, pg, sp, rp);
        }
      }

      char probe_hex[28] = {0};
      fmt_hex16(probe, sizeof(probe), probe_hex, sizeof(probe_hex));
      cJSON_AddBoolToObject(game, "mprotectOk", mprotect_rc == 0);
      cJSON_AddBoolToObject(game, "ptAttachOk", attach_rc == 0);
      cJSON_AddBoolToObject(game, "ptReadOk", pt_rc >= 0);
      cJSON_AddStringToObject(game, "probeBytes", probe_hex);
      cJSON_AddNumberToObject(game, "mprotectRc", mprotect_rc);
      cJSON_AddNumberToObject(game, "ptAttachRc", attach_rc);
      cJSON_AddNumberToObject(game, "ptReadRc", pt_rc);
    }
  }

  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}

void
handle_api_dev_shutdown(int fd, const char *method, const char *query, const char *client_ip) {
  (void)query;
  int enabled = 0;
  int delay_ms = 700;
  char body[512];

  if (strcmp(method, "POST") != 0) {
    http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\",\"message\":\"Use POST for shutdown.\"}");
    return;
  }

  pthread_mutex_lock(&g_cfg_lock);
  enabled = g_cfg.dev_reload_enabled;
  delay_ms = g_cfg.dev_shutdown_delay_ms;
  pthread_mutex_unlock(&g_cfg_lock);

  if (!enabled) {
    cr_log("warn", "dev", "shutdown rejected: dev reload disabled");
    http_send_json(fd, 403,
                   "{\"ok\":false,\"error\":\"dev_reload_disabled\",\"message\":\"Dev reload mode is disabled.\"}");
    return;
  }

  cr_log("info", "dev", "shutdown request received from %s", client_ip ? client_ip : "unknown");
  cr_log("info", "dev", "shutdown accepted; exiting in %dms", delay_ms);
  snprintf(body, sizeof(body),
           "{\"ok\":true,\"message\":\"CheatRunner is shutting down. Send the new ELF from your PC.\",\"delayMs\":%d}",
           delay_ms);
  http_send_json(fd, 200, body);
  cheatrunner_request_shutdown(delay_ms);
}

/* ── /api/user/context ──────────────────────────────────────────── */
void
handle_api_user_context(int fd) {
  char uid_cfg[32] = "auto";
  pthread_mutex_lock(&g_cfg_lock);
  snprintf(uid_cfg, sizeof(uid_cfg), "%s", g_cfg.launch_user_id);
  pthread_mutex_unlock(&g_cfg_lock);

  /* Determine configured mode */
  const char *mode = "auto";
  char mode_user[24] = "";
  if (strcasecmp(uid_cfg, "none") == 0) {
    mode = "none";
  } else if (strcasecmp(uid_cfg, "auto") != 0 && uid_cfg[0] != '\0') {
    mode = "config";
    snprintf(mode_user, sizeof(mode_user), "%s", uid_cfg);
  }

  /* Foreground user */
  uint32_t fguser = 0xFFFFFFFFu;
  int fg_valid = (sceUserServiceGetForegroundUser(&fguser) == 0 &&
                  fguser != 0xFFFFFFFFu && (int32_t)fguser != -1);
  char fg_str[24];
  if (fg_valid) snprintf(fg_str, sizeof(fg_str), "0x%08X", fguser);
  else          snprintf(fg_str, sizeof(fg_str), "none");

  /* Login user list */
  uint32_t uid_list[16] = {0};
  size_t uid_actual = 0;
  int list_ok = 0;
  char users_json[320] = "[]";
#if CHEATRUNNER_HAVE_SCE_USER_LIST
  list_ok = (sceUserServiceGetLoginUserIdList(uid_list, 16, &uid_actual) == 0);
  if (list_ok && uid_actual > 0) {
    char tmp[320];
    int pos = 0;
    tmp[pos++] = '[';
    for (size_t i = 0; i < uid_actual && i < 16 && pos < (int)sizeof(tmp) - 20; i++) {
      if (i > 0) tmp[pos++] = ',';
      pos += snprintf(tmp + pos, sizeof(tmp) - pos, "\"0x%08X\"", uid_list[i]);
    }
    if (pos < (int)sizeof(tmp) - 2) { tmp[pos++] = ']'; tmp[pos] = '\0'; }
    snprintf(users_json, sizeof(users_json), "%s", tmp);
  }
#endif

  /* Resolve which user would be used for launch */
  const char *src = "none";
  char resolved_str[24] = "none";
  int resolved_ok = 0;
  if (strcmp(mode, "none") == 0) {
    src = "none";
  } else if (strcmp(mode, "config") == 0) {
    src = "config";
    snprintf(resolved_str, sizeof(resolved_str), "%s", mode_user);
    resolved_ok = 1;
  } else { /* auto */
    if (fg_valid) {
      src = "foreground";
      snprintf(resolved_str, sizeof(resolved_str), "0x%08X", fguser);
      resolved_ok = 1;
    }
#if CHEATRUNNER_HAVE_SCE_USER_LIST
    else if (list_ok && uid_actual == 1 &&
             uid_list[0] != 0xFFFFFFFFu && (int32_t)uid_list[0] != -1) {
      src = "login_list";
      snprintf(resolved_str, sizeof(resolved_str), "0x%08X", uid_list[0]);
      resolved_ok = 1;
    }
#endif
  }

  const char *msg = resolved_ok ? "user resolved"
    : (strcmp(mode, "none") == 0 ? "user context disabled" : "no valid user found");

  char body[896];
  snprintf(body, sizeof(body),
    "{\"ok\":true,"
    "\"configured\":{\"mode\":\"%s\",\"userId\":\"%s\"},"
    "\"foreground\":{\"ok\":%s,\"userId\":\"%s\"},"
    "\"loginUserList\":{\"available\":%s,\"count\":%zu,\"users\":%s},"
    "\"resolved\":{\"ok\":%s,\"source\":\"%s\",\"userId\":\"%s\"},"
    "\"message\":\"%s\"}",
    mode, mode_user,
    fg_valid ? "true" : "false", fg_str,
    list_ok ? "true" : "false", uid_actual, users_json,
    resolved_ok ? "true" : "false", src, resolved_str,
    msg);
  http_send_json(fd, 200, body);
}

/* ── /api/diag/title ────────────────────────────────────────────── */
void
handle_api_diag_title(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  cJSON *out = cJSON_CreateObject();
  if (!out) {
    http_send_oom(fd);
    return;
  }
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddStringToObject(out, "titleId", title_id);

  running_game_state_t rgs;
  running_state_get(&rgs);
  int title_match = rgs.running && strcmp(rgs.title_id, title_id) == 0;
  {
    cJSON *r = cJSON_AddObjectToObject(out, "running");
    if (r) {
      cJSON_AddBoolToObject(r, "running", rgs.running);
      cJSON_AddBoolToObject(r, "titleMatch", title_match);
      cJSON_AddStringToObject(r, "runningTitleId", rgs.title_id);
      cJSON_AddNumberToObject(r, "pid", (double)(rgs.running ? (int)rgs.pid : -1));
      cJSON_AddNumberToObject(r, "appId", (double)rgs.app_id);
      char base_hex[32]; snprintf(base_hex, sizeof(base_hex), "0x%lx", (long)rgs.image_base);
      cJSON_AddStringToObject(r, "imageBase", base_hex);
    }
  }

  {
    char ls_phase[64] = {0}, ls_title[12] = {0}, ls_message[160] = {0};
    char ls_method[64] = {0}, ls_hex[16] = {0};
    int ls_busy = 0, ls_rc = 0, ls_verified = 0;
    pthread_mutex_lock(&g_launch_status_lock);
    snprintf(ls_phase,   sizeof(ls_phase),   "%s", g_launch_status.phase);
    snprintf(ls_title,   sizeof(ls_title),   "%s", g_launch_status.title_id);
    snprintf(ls_message, sizeof(ls_message), "%s", g_launch_status.message);
    snprintf(ls_method,  sizeof(ls_method),  "%s", g_launch_status.method);
    snprintf(ls_hex,     sizeof(ls_hex),     "%s", g_launch_status.hex);
    ls_busy     = g_launch_status.busy;
    ls_rc       = g_launch_status.rc;
    ls_verified = g_launch_status.verified;
    pthread_mutex_unlock(&g_launch_status_lock);
    cJSON *ls = cJSON_AddObjectToObject(out, "launchStatus");
    if (ls) {
      cJSON_AddBoolToObject(ls, "busy", ls_busy);
      cJSON_AddStringToObject(ls, "phase", ls_phase);
      cJSON_AddStringToObject(ls, "titleId", ls_title);
      cJSON_AddStringToObject(ls, "message", ls_message);
      cJSON_AddStringToObject(ls, "method", ls_method);
      cJSON_AddStringToObject(ls, "rcHex", ls_hex);
      cJSON_AddNumberToObject(ls, "rc", ls_rc);
      cJSON_AddBoolToObject(ls, "verified", ls_verified);
    }
  }

  {
    char cheat_path[256] = {0};
    int cheat_kind = 0;
    char cheat_err[128] = {0};
    int found = 0;
    int mod_count = 0;
    cJSON *root = load_cheat_json_root_for_title_ex(title_id, cheat_err, sizeof(cheat_err), cheat_path, sizeof(cheat_path), &cheat_kind);
    if (root) {
      found = 1;
      cJSON *mods = cJSON_GetObjectItem(root, "mods");
      if (!cJSON_IsArray(mods)) mods = cJSON_GetObjectItem(root, "patches");
      if (cJSON_IsArray(mods)) mod_count = cJSON_GetArraySize(mods);
      cJSON_Delete(root);
    }
    const char *fmt_str = cheat_kind == 1 ? "json" : (cheat_kind == 2 ? "shn" : (cheat_kind == 3 ? "mc4" : "none"));
    cJSON *cf = cJSON_AddObjectToObject(out, "cheat");
    if (cf) {
      cJSON_AddBoolToObject(cf, "found", found);
      cJSON_AddStringToObject(cf, "format", fmt_str);
      cJSON_AddStringToObject(cf, "path", found ? cheat_path : "");
      cJSON_AddNumberToObject(cf, "modCount", mod_count);
      if (!found && cheat_err[0]) cJSON_AddStringToObject(cf, "error", cheat_err);
    }
  }

  {
    cheat_file_search_t cfs;
    find_cheat_candidates(title_id, &cfs);
    cJSON *scan = cJSON_AddObjectToObject(out, "scan");
    if (scan) {
      cJSON_AddNumberToObject(scan, "candidateCount", cfs.candidate_count);
      cJSON_AddStringToObject(scan, "preferredVer", cfs.preferred_ver);
      cJSON *cands = cJSON_AddArrayToObject(scan, "candidates");
      if (cands) {
        for (int i = 0; i < cfs.candidate_count; i++) {
          cJSON *ce = cJSON_CreateObject();
          const char *p = cfs.candidates[i].path;
          const char *base = strrchr(p, '/');
          cJSON_AddStringToObject(ce, "file", base ? base + 1 : p);
          cJSON_AddStringToObject(ce, "path", p);
          cJSON_AddNumberToObject(ce, "score", cfs.candidates[i].score);
          const char *fmt = cfs.candidates[i].kind == 1 ? "json" : (cfs.candidates[i].kind == 2 ? "shn" : "mc4");
          cJSON_AddStringToObject(ce, "format", fmt);
          cJSON_AddItemToArray(cands, ce);
        }
      }
    }
  }

  {
    char uid_cfg[32] = {0};
    pthread_mutex_lock(&g_cfg_lock);
    snprintf(uid_cfg,  sizeof(uid_cfg),  "%s", g_cfg.launch_user_id);
    pthread_mutex_unlock(&g_cfg_lock);
    cJSON *cfg = cJSON_AddObjectToObject(out, "config");
    if (cfg) {
      cJSON_AddStringToObject(cfg, "launchUserId", uid_cfg[0] ? uid_cfg : "auto");
    }
  }

  {
    const cr_priv_status_t *ps = cr_priv_get();
    cJSON *priv = cJSON_AddObjectToObject(out, "privileges");
    if (priv) {
      cJSON_AddNumberToObject(priv, "uid", ps->uid);
      cJSON_AddNumberToObject(priv, "gid", ps->gid);
      cJSON_AddBoolToObject(priv, "uidOk", ps->uid_ok);
      cJSON_AddBoolToObject(priv, "sandboxEscaped", ps->sandbox_ok);
      cJSON_AddBoolToObject(priv, "sceAuthOk", ps->authid_ok);
      cJSON_AddBoolToObject(priv, "sceCapsOk", ps->caps_ok);
      cJSON_AddBoolToObject(priv, "kernelRwOk", ps->kernel_rw_ok);
      cJSON_AddBoolToObject(priv, "kernelMprotectOk", ps->kernel_mprotect_ok);
      cJSON_AddBoolToObject(priv, "canPatchGameMemory", ps->can_patch_game);
      cJSON *wa = cJSON_AddArrayToObject(priv, "warnings");
      if (wa) {
        for (int i = 0; i < ps->n_warnings; i++) {
          cJSON *ws = cJSON_CreateString(ps->warnings[i]);
          if (ws) cJSON_AddItemToArray(wa, ws);
        }
      }
    }
  }

  {
    last_game_exit_t lge;
    pthread_mutex_lock(&g_last_game_exit_lock);
    lge = g_last_game_exit;
    pthread_mutex_unlock(&g_last_game_exit_lock);
    if (lge.title_id[0]) {
      cJSON *lge_j = cJSON_AddObjectToObject(out, "lastGameExit");
      if (lge_j) {
        cJSON_AddStringToObject(lge_j, "titleId", lge.title_id);
        char appid_hex[16]; snprintf(appid_hex, sizeof(appid_hex), "0x%x", lge.app_id);
        cJSON_AddStringToObject(lge_j, "appId", appid_hex);
        cJSON_AddNumberToObject(lge_j, "pid", (double)lge.pid);
        cJSON_AddStringToObject(lge_j, "reason", lge.reason);
        cJSON_AddNumberToObject(lge_j, "tsMs", (double)lge.ts_ms);
        cJSON_AddNumberToObject(lge_j, "elapsedAfterLastCheatMs", (double)lge.elapsed_after_last_cheat_ms);
        cJSON_AddNumberToObject(lge_j, "lastModIndex", (double)lge.last_mod_index);
        cJSON_AddStringToObject(lge_j, "lastModName", lge.last_mod_name);
        cJSON_AddBoolToObject(lge_j, "suspected", lge.suspected);
      }
    }
  }

  {
    last_apply_rec_t lar;
    pthread_mutex_lock(&g_last_apply_lock);
    lar = g_last_apply_rec;
    pthread_mutex_unlock(&g_last_apply_lock);
    if (lar.ts_ms > 0) {
      cJSON *lar_j = cJSON_AddObjectToObject(out, "lastApply");
      if (lar_j) {
        cJSON_AddStringToObject(lar_j, "titleId", lar.title_id);
        cJSON_AddNumberToObject(lar_j, "modIndex", (double)lar.mod_index);
        cJSON_AddStringToObject(lar_j, "modName", lar.mod_name);
        cJSON_AddBoolToObject(lar_j, "effectiveOn", lar.effective_on);
        cJSON_AddBoolToObject(lar_j, "ok", lar.ok);
        cJSON_AddBoolToObject(lar_j, "hookCodecave", lar.hook_codecave);
        cJSON_AddNumberToObject(lar_j, "caveCount", (double)lar.cave_count);
        cJSON_AddNumberToObject(lar_j, "hookCount", (double)lar.hook_count);
        cJSON_AddNumberToObject(lar_j, "tsMs", (double)lar.ts_ms);
        cJSON *writes_j = cJSON_AddArrayToObject(lar_j, "writes");
        if (writes_j) {
          for (int _i = 0; _i < lar.write_count; _i++) {
            cJSON *w = cJSON_CreateObject();
            if (w) {
              char addr_hex[20]; snprintf(addr_hex, sizeof(addr_hex), "0x%lx", (long)lar.writes[_i].addr);
              cJSON_AddStringToObject(w, "addr", addr_hex);
              cJSON_AddNumberToObject(w, "len", (double)lar.writes[_i].len);
              cJSON_AddBoolToObject(w, "isCave", lar.writes[_i].is_cave);
              cJSON_AddItemToArray(writes_j, w);
            }
          }
        }
      }
    }
  }
  cJSON_AddBoolToObject(out, "scannerPausedDuringApply", g_cheat_applying ? 1 : 0);

  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}

/* ── /api/cheats/scan ───────────────────────────────────────────── */
void
handle_api_cheats_scan(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  cheat_file_search_t cfs;
  memset(&cfs, 0, sizeof(cfs));
  find_cheat_candidates(title_id, &cfs);

  cJSON *out = cJSON_CreateObject();
  if (!out) {
    http_send_oom(fd);
    return;
  }
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddStringToObject(out, "titleId", title_id);
  cJSON_AddStringToObject(out, "dir", CHEATRUNNER_CHEATS_DIR);
  cJSON_AddNumberToObject(out, "candidateCount", cfs.candidate_count);
  cJSON_AddStringToObject(out, "preferredVersion", cfs.preferred_ver);
  cJSON_AddStringToObject(out, "selected", cfs.best_path[0] ? cfs.best_path : "");
  const char *sfmt = cfs.best_kind == 1 ? "json" : (cfs.best_kind == 2 ? "shn" : (cfs.best_kind == 3 ? "mc4" : "none"));
  cJSON_AddStringToObject(out, "selectedFormat", cfs.best_path[0] ? sfmt : "none");
  cJSON *cands = cJSON_AddArrayToObject(out, "candidates");
  if (cands) {
    for (int i = 0; i < cfs.candidate_count; i++) {
      cJSON *ce = cJSON_CreateObject();
      const char *p = cfs.candidates[i].path;
      const char *base = strrchr(p, '/');
      cJSON_AddStringToObject(ce, "file", base ? base + 1 : p);
      cJSON_AddStringToObject(ce, "path", p);
      cJSON_AddNumberToObject(ce, "score", cfs.candidates[i].score);
      const char *fmt = cfs.candidates[i].kind == 1 ? "json" : (cfs.candidates[i].kind == 2 ? "shn" : "mc4");
      cJSON_AddStringToObject(ce, "format", fmt);
      cJSON_AddBoolToObject(ce, "selected", strcmp(p, cfs.best_path) == 0);
      cJSON_AddItemToArray(cands, ce);
    }
  }
  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}

/* ── /api/cheats/mc4-debug ──────────────────────────────────────── */
void
handle_api_cheats_mc4_debug(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 || !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }
  char cheat_path[256] = {0};
  char err[128] = {0};
  int cheat_kind = 0;
  cJSON *root = load_cheat_json_root_for_title_ex(title_id, err, sizeof(err), cheat_path, sizeof(cheat_path), &cheat_kind);
  if (!root) {
    char body[256]; snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err);
    http_send_json(fd, 404, body);
    return;
  }
  if (cheat_kind == 1) {
    cJSON_Delete(root);
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"cheat is JSON format; mc4-debug is for SHN/MC4 only\"}");
    return;
  }
  cJSON *out = cJSON_CreateObject();
  if (!out) {
    cJSON_Delete(root);
    http_send_oom(fd);
    return;
  }
  cJSON_AddBoolToObject(out, "ok", 1);
  cJSON_AddStringToObject(out, "titleId", title_id);
  cJSON_AddStringToObject(out, "cheatPath", cheat_path);
  cJSON_AddStringToObject(out, "format", cheat_kind == 2 ? "shn" : "mc4");

  if (cheat_kind == 3) {
    char *txt = NULL;
    if (read_file_text(cheat_path, &txt) == 0 && txt) {
      size_t xml_sz = 0;
      char *xml = mc4_decrypt_to_xml(txt, strlen(txt), &xml_sz);
      free(txt);
      if (xml) {
        char excerpt[2048];
        snprintf(excerpt, sizeof(excerpt), "%.*s", (int)(xml_sz < 1999u ? xml_sz : 1999u), xml);
        cJSON_AddStringToObject(out, "xmlExcerpt", excerpt);
        free(xml);
      } else {
        cJSON_AddStringToObject(out, "xmlExcerpt", "(decrypt failed)");
      }
    }
  }

  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods)) mods = cJSON_GetObjectItem(root, "patches");
  cJSON *mod_arr = cJSON_AddArrayToObject(out, "mods");
  if (cJSON_IsArray(mods) && mod_arr) {
    cJSON *mod = NULL;
    cJSON_ArrayForEach(mod, mods) {
      cJSON *mo = cJSON_CreateObject();
      if (!mo) break;
      cJSON *n = cJSON_GetObjectItem(mod, "name");
      cJSON_AddStringToObject(mo, "name", cJSON_IsString(n) ? n->valuestring : "");
      cJSON *mem = cJSON_GetObjectItem(mod, "memory");
      if (!cJSON_IsArray(mem)) mem = cJSON_GetObjectItem(mod, "patches");
      cJSON *patch_arr = cJSON_AddArrayToObject(mo, "patches");
      if (cJSON_IsArray(mem) && patch_arr) {
        cJSON *m = NULL;
        int pi = 0;
        cJSON_ArrayForEach(m, mem) {
          cJSON *pe = cJSON_CreateObject();
          if (!pe) break;
          cJSON_AddNumberToObject(pe, "index", pi++);
          cJSON *on_j  = cJSON_GetObjectItem(m, "on");
          cJSON *off_j = cJSON_GetObjectItem(m, "off");
          cJSON *exp_j = cJSON_GetObjectItem(m, "expected");
          cJSON *abs_j = cJSON_GetObjectItem(m, "absolute");
          cJSON_AddStringToObject(pe, "valueOn",  cJSON_IsString(on_j)  ? on_j->valuestring  : "");
          cJSON_AddStringToObject(pe, "valueOff", cJSON_IsString(off_j) ? off_j->valuestring : "");
          cJSON_AddStringToObject(pe, "expected", cJSON_IsString(exp_j) ? exp_j->valuestring : "");
          cJSON_AddBoolToObject(pe, "absoluteFlag", cJSON_IsTrue(abs_j));
          size_t elen = 0;
          if (cJSON_IsString(exp_j) && exp_j->valuestring && exp_j->valuestring[0]) {
            size_t tmp_need = hex_bytes_need(exp_j->valuestring);
            uint8_t *tmp CR_AUTOFREE = malloc(tmp_need);
            if (tmp) parse_hex_bytes_checked(exp_j->valuestring, tmp, tmp_need, &elen);
          }
          int off_reliable = (elen > 0);
          cJSON_AddBoolToObject(pe, "offReliable", off_reliable);
          cJSON_AddStringToObject(pe, "offNote",
            off_reliable ? "expected field present — off comparison is reliable" :
                           "no expected field — ValueOff may not match original bytes");
          cJSON_AddItemToArray(patch_arr, pe);
        }
      }
      cJSON_AddItemToArray(mod_arr, mo);
    }
  }
  cJSON_Delete(root);
  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  if (!s) { http_send_oom(fd); return; }
  http_send_json(fd, 200, s);
  free(s);
}

void
handle_api_cheats_disable_all(int fd, const char *query) {
  char raw_title_id[32] = {0};
  char title_id[10] = {0};
  if (query_value(query, "titleId", raw_title_id, sizeof(raw_title_id)) != 0 ||
      !title_id_normalize(raw_title_id, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad titleId\"}");
    return;
  }

  running_game_state_t rs;
  running_state_get(&rs);
  if (!rs.running || strcmp(rs.title_id, title_id) != 0) {
    http_send_json(fd, 200, "{\"ok\":true,\"disabled\":0,\"reason\":\"game_not_running\"}");
    return;
  }

  char err[128] = {0};
  char path_out[256] = {0};
  int kind_out = 0;
  cJSON *root = load_cheat_json_root_for_title_ex(title_id, err, sizeof(err),
                                                   path_out, sizeof(path_out), &kind_out);
  if (!root) {
    http_send_json(fd, 404, "{\"ok\":false,\"error\":\"no cheat file\"}");
    return;
  }

  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  int n = cJSON_IsArray(mods) ? cJSON_GetArraySize(mods) : 0;
  int disabled = 0;
  int need_delay = 0;
  for (int i = 0; i < n; i++) {
    if (mod_enabled_check(title_id, rs.pid, i)) {
      if (need_delay) usleep(1000000);
      char apply_err[256] = {0};
      /* Clear the cooldown so rapid disables are not blocked. */
      g_last_apply_at_ms = 0;
      if (apply_cheat_json(title_id, i, 0, apply_err, sizeof(apply_err)) == 0)
        disabled++;
      need_delay = 1;
    }
  }
  cJSON_Delete(root);

  char body_buf[128];
  snprintf(body_buf, sizeof(body_buf), "{\"ok\":true,\"disabled\":%d}", disabled);
  http_send_json(fd, 200, body_buf);
}

void
http_send_png_asset(int fd) {
  char header[256];
  size_t len = sizeof(g_cheatrunner_png);
  int n = snprintf(header, sizeof(header),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: image/png\r\n"
    "Content-Length: %u\r\n"
    "Connection: close\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: public, max-age=86400\r\n"
    "\r\n",
    (unsigned int)len);
  if (n > 0) {
    if (socket_send_all(fd, header, (size_t)n) != 0) {
      return;
    }
    (void)socket_send_all(fd, g_cheatrunner_png, len);
  }
}

void
http_route(int fd, const char *method, const char *path, const char *query,
           const char *client_ip, const char *body, size_t body_len,
           int accepts_gzip) {
  if (!g_startup_ms) {
    struct timespec _ts; clock_gettime(CLOCK_MONOTONIC, &_ts);
    g_startup_ms = (long long)_ts.tv_sec * 1000LL + (long long)(_ts.tv_nsec / 1000000L);
  }

  if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
    http_send_json(fd, 405, "{\"ok\":false,\"error\":\"GET/POST only\"}");
    return;
  }
  if (cr_api_dashboard_handle(fd, method, path, query, body, body_len, accepts_gzip)) return;
  if (cr_api_games_handle(fd, method, path, query, body, body_len)) return;
  if (cr_api_cheats_handle(fd, method, path, query, body, body_len)) return;
  if (cr_api_cheat_profiles_handle(fd, method, path, query, body, body_len)) return;
  if (cr_api_patches_handle(fd, method, path, query, body, body_len)) return;
  if (cr_api_patch_profiles_handle(fd, method, path, query, body, body_len)) return;
  if (cr_api_sources_handle(fd, method, path, query, body, body_len)) return;
  if (cr_api_logs_handle(fd, method, path, query, body, body_len)) return;
  /* shutdown needs client_ip - handle directly */
  if (!strcmp(path, "/api/dev/shutdown")) {
    handle_api_dev_shutdown(fd, method, query, client_ip);
    return;
  }
  if (cr_api_dev_handle(fd, method, path, query, body, body_len)) return;
  http_send_json(fd, 404, "{\"ok\":false,\"error\":\"not found\"}");
}






