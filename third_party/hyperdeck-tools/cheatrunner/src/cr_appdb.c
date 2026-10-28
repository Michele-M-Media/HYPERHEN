#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cr_appdb.h"
#include "cr_config.h"
#include "cr_log.h"
#include "cr_paths.h"
#include "cr_titles.h"
#include "ps5sdk_compat.h"

#if CHEATRUNNER_HAVE_SQLITE_APPDB
#include "third_party/sqlite3.h"
#endif

static int
find_game_index(game_entry_t *entries, size_t count, const char *title_id) {
  for (size_t i = 0; i < count; i++) {
    if (!strcmp(entries[i].title_id, title_id)) {
      return (int)i;
    }
  }
  return -1;
}

static int
title_lookup_cache_read(const char *title_id, char *out, size_t out_size) {
  if (!title_id || !out || out_size == 0) {
    return 0;
  }
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

static void
scan_game_dir(const char *root, game_entry_t *entries, size_t *count, size_t max_entries) {
  DIR *d = opendir(root);
  if (!d) {
    return;
  }
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    if (*count >= max_entries) {
      break;
    }
    char full[512];
    struct stat st;
    snprintf(full, sizeof(full), "%s/%s", root, ent->d_name);
    if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
      continue;
    }
    char title_id[16];
    char title_name[256];
    char content_id[96] = {0};
    if (load_title_meta(full, ent->d_name, title_id, sizeof(title_id), title_name, sizeof(title_name)) != 0) {
      continue;
    }
    char norm_id[10];
    if (!title_id_normalize(title_id, norm_id) || !is_game_title_id(norm_id)) {
      continue;
    }
    snprintf(title_id, sizeof(title_id), "%s", norm_id);
    int idx = find_game_index(entries, *count, title_id);
    if (idx >= 0) {
      if (cr_title_name_is_unresolved(entries[idx].title_id, entries[idx].title_name) &&
          !cr_title_name_is_unresolved(title_id, title_name)) {
        snprintf(entries[idx].title_name, sizeof(entries[idx].title_name), "%s", title_name);
        snprintf(entries[idx].name_source, sizeof(entries[idx].name_source), "%s", "param_json");
        entries[idx].name_resolved = 1;
      }
      if (!entries[idx].content_id[0]) {
        read_param_value_by_title_id(title_id, "contentId", content_id, sizeof(content_id));
        if (content_id[0]) {
          snprintf(entries[idx].content_id, sizeof(entries[idx].content_id), "%s", content_id);
        }
      }
      continue;
    }
    read_param_value_by_title_id(title_id, "contentId", content_id, sizeof(content_id));
    const char *source = "param_json";
    if (cr_title_name_is_unresolved(title_id, title_name)) {
      if (title_lookup_cache_read(title_id, title_name, sizeof(title_name))) {
        source = "cache_lookup";
      } else {
        source = "fallback_title_id";
      }
    }
    snprintf(entries[*count].title_id, sizeof(entries[*count].title_id), "%s", title_id);
    snprintf(entries[*count].title_name, sizeof(entries[*count].title_name), "%s",
             cr_title_name_is_unresolved(title_id, title_name) ? title_id : title_name);
    snprintf(entries[*count].content_id, sizeof(entries[*count].content_id), "%s", content_id);
    snprintf(entries[*count].name_source, sizeof(entries[*count].name_source), "%s", source);
    entries[*count].name_resolved = !cr_title_name_is_unresolved(title_id, entries[*count].title_name);
    (*count)++;
  }
  closedir(d);
}

static int
collect_games(game_entry_t *entries, size_t *count) {
  scan_game_dir("/user/appmeta", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/user/appmeta/external", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/user/app", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/user/patch", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/mnt/ext0/user/app", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/mnt/ext1/user/app", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/mnt/ext0/user/patch", entries, count, CR_APPDB_MAX_GAMES);
  scan_game_dir("/mnt/ext1/user/patch", entries, count, CR_APPDB_MAX_GAMES);
  return 0;
}

#if CHEATRUNNER_HAVE_SQLITE_APPDB
/* SQLite is built with SQLITE_THREADSAFE=0; all entry points must hold this or concurrent calls corrupt state. */
static pthread_mutex_t g_sqlite_lock = PTHREAD_MUTEX_INITIALIZER;

static int
appdb_table_exists(sqlite3 *db, const char *table) {
  sqlite3_stmt *st = NULL;
  int found = 0;
  const char *sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?1;";
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
    return 0;
  }
  sqlite3_bind_text(st, 1, table, -1, NULL);
  if (sqlite3_step(st) == SQLITE_ROW) {
    found = 1;
  }
  sqlite3_finalize(st);
  return found;
}

static int
appdb_table_has_column(sqlite3 *db, const char *table, const char *column) {
  sqlite3_stmt *st = NULL;
  char sql[128];
  snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
    return 0;
  }
  int found = 0;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char *name = sqlite3_column_text(st, 1);
    if (name && !strcmp((const char *)name, column)) {
      found = 1;
      break;
    }
  }
  sqlite3_finalize(st);
  return found;
}

static int
cmp_str(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

/* Index of the logged-in user's table, or -1 when it cannot be told apart. */
static int
appdb_user_table_index(char tables[][96], int n) {
  if (n == 1) {
    return 0;
  }
  uint32_t uid = 0xFFFFFFFFu;
  if (sceUserServiceGetForegroundUser(&uid) != 0 || uid == 0xFFFFFFFFu) {
    uid = 0xFFFFFFFFu;
#if CHEATRUNNER_HAVE_SCE_USER_LIST
    uint32_t list[16] = {0};
    size_t actual = 0;
    if (sceUserServiceGetLoginUserIdList(list, 16, &actual) == 0 && actual == 1) {
      uid = list[0];
    }
#endif
  }
  if (uid == 0xFFFFFFFFu || uid == 0) {
    cr_log("warn", "appdb", "play history: no logged-in user");
    return -1;
  }
  char want[2][48];
  snprintf(want[0], sizeof(want[0]), "tbl_iconinfo_%x", uid);
  snprintf(want[1], sizeof(want[1]), "tbl_iconinfo_%08x", uid);
  for (int t = 0; t < n; t++) {
    if (!strcasecmp(tables[t], want[0]) || !strcasecmp(tables[t], want[1])) {
      return t;
    }
  }
  /* Names did not match: tables and /user/home folders are in the same user order. */
  char users[16][64];
  int un = 0;
  DIR *d = opendir("/user/home");
  struct dirent *e;
  while (d && (e = readdir(d)) != NULL && un < 16) {
    if (e->d_name[0] != '.') {
      snprintf(users[un++], sizeof(users[0]), "%s", e->d_name);
    }
  }
  if (d) {
    closedir(d);
  }
  qsort(users, (size_t)un, sizeof(users[0]), cmp_str);
  for (int u = 0; u < un; u++) {
    if (u < n && (!strcasecmp(users[u] , want[0] + 13) || !strcasecmp(users[u], want[1] + 13))) {
      return u;
    }
  }
  cr_log("warn", "appdb", "play history: user 0x%08x not matched to %d tables", uid, n);
  return -1;
}

/* Play time and last played of the logged-in user (tbl_iconinfo_<user>), the numbers the PS5 home screen shows.
 * Returns 1 when merged, 0 when there are no such tables, -1 when the user's table is unknown. */
static int
appdb_merge_activity(sqlite3 *db, game_entry_t *entries, size_t count) {
  char tables[16][96];
  int n = 0, merged = 0;
  sqlite3_stmt *ts = NULL;
  if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_iconinfo_%' ORDER BY name", -1, &ts, NULL) != SQLITE_OK) {
    return 0;
  }
  while (n < 16 && sqlite3_step(ts) == SQLITE_ROW) {
    const unsigned char *name = sqlite3_column_text(ts, 0);
    if (name) {
      snprintf(tables[n++], sizeof(tables[0]), "%s", (const char *)name);
    }
  }
  sqlite3_finalize(ts);
  if (n == 0) {
    return 0;
  }
  int pick = appdb_user_table_index(tables, n);
  if (pick < 0) {
    return -1;
  }
  static char logged[96];
  if (strcmp(logged, tables[pick]) != 0) {
    snprintf(logged, sizeof(logged), "%s", tables[pick]);
    cr_log("info", "appdb", "play history from %s (%d table(s))", tables[pick], n);
  }

  if (appdb_table_has_column(db, tables[pick], "playedTimeOnConsole")) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT titleId, COALESCE(playedTimeOnConsole,0), CAST(strftime('%%s', lastAccessTime) AS INTEGER) FROM %s", tables[pick]);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
      while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *id = sqlite3_column_text(st, 0);
        char norm[10];
        if (!id || !title_id_normalize((const char *)id, norm)) {
          continue;
        }
        int k = find_game_index(entries, count, norm);
        if (k < 0) {
          continue;
        }
        sqlite3_int64 played = sqlite3_column_int64(st, 1);
        sqlite3_int64 last = sqlite3_column_int64(st, 2);
        entries[k].play_time_seconds = played > 0 ? (uint64_t)played : 0;
        entries[k].last_access_time = last > 0 ? (uint64_t)last : 0;
        merged = 1;
      }
      sqlite3_finalize(st);
    }
  }
  return merged ? 1 : -1;
}

static int
appdb_read_appinfo_name(sqlite3 *db, const char *title_id, char *out, size_t out_size) {
  static const char * const keys[] = {
    "TITLE_01","TITLE_02","TITLE_03","TITLE_04","TITLE_05","TITLE_06","TITLE_07","TITLE_08",
    "TITLE_09","TITLE_10","TITLE_11","TITLE_12","TITLE_13","TITLE_14","TITLE_15","TITLE_16",
    "TITLE_17","TITLE_18","TITLE_19","TITLE_20","TITLE_21","TITLE_22","TITLE_00","TITLE", NULL,
  };
  const char *sql = "SELECT val FROM tbl_appinfo WHERE titleId=?1 AND key=?2 AND val IS NOT NULL AND val!='' LIMIT 1;";
  out[0] = '\0';
  for (int i = 0; keys[i]; i++) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
      return 0;
    }
    sqlite3_bind_text(st, 1, title_id, -1, NULL);
    sqlite3_bind_text(st, 2, keys[i], -1, NULL);
    if (sqlite3_step(st) == SQLITE_ROW) {
      const unsigned char *v = sqlite3_column_text(st, 0);
      if (v && v[0] && !cr_title_name_is_unresolved(title_id, (const char *)v)) {
        snprintf(out, out_size, "%s", (const char *)v);
        sqlite3_finalize(st);
        return 1;
      }
    }
    sqlite3_finalize(st);
  }
  return 0;
}

static void
appdb_merge_game(game_entry_t *entries, size_t *count, const char *title_id, const char *title_name,
                 const char *content_id, const char *name_source) {
  if (!title_id || !is_valid_title_id(title_id) || !is_game_title_id(title_id) || *count >= CR_APPDB_MAX_GAMES) {
    return;
  }
  int idx = find_game_index(entries, *count, title_id);
  if (idx >= 0) {
    if (!cr_title_name_is_unresolved(title_id, title_name) &&
        cr_title_name_is_unresolved(entries[idx].title_id, entries[idx].title_name)) {
      snprintf(entries[idx].title_name, sizeof(entries[idx].title_name), "%s", title_name);
      snprintf(entries[idx].name_source, sizeof(entries[idx].name_source), "%s", name_source ? name_source : "");
      entries[idx].name_resolved = 1;
    }
    if (content_id && content_id[0] && !entries[idx].content_id[0]) {
      snprintf(entries[idx].content_id, sizeof(entries[idx].content_id), "%s", content_id);
    }
    return;
  }
  snprintf(entries[*count].title_id, sizeof(entries[*count].title_id), "%s", title_id);
  snprintf(entries[*count].title_name, sizeof(entries[*count].title_name), "%s",
           (!cr_title_name_is_unresolved(title_id, title_name)) ? title_name : title_id);
  snprintf(entries[*count].content_id, sizeof(entries[*count].content_id), "%s", content_id ? content_id : "");
  snprintf(entries[*count].name_source, sizeof(entries[*count].name_source), "%s",
           name_source ? name_source : "fallback_title_id");
  entries[*count].name_resolved = !cr_title_name_is_unresolved(title_id, entries[*count].title_name);
  (*count)++;
}

static int
appdb_collect_games_sqlite(game_entry_t *entries, size_t *count) {
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  const char *sql = NULL;
  int rc = -1;

  pthread_mutex_lock(&g_sqlite_lock);
  if (sqlite3_open_v2("/system_data/priv/mms/app.db", &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK) {
    cr_log("debug", "appdb", "sqlite3_open failed: %s", db ? sqlite3_errmsg(db) : "alloc failed");
    if (db) {
      sqlite3_close(db);
    }
    pthread_mutex_unlock(&g_sqlite_lock);
    return -1;
  }
  sqlite3_busy_timeout(db, 3000);

  int has_appbrowse = appdb_table_exists(db, "tbl_appbrowse");
  int has_contentinfo = appdb_table_exists(db, "tbl_contentinfo");
  int has_browse_name = has_appbrowse && appdb_table_has_column(db, "tbl_appbrowse", "titleName");
  int has_content_name = has_contentinfo && appdb_table_has_column(db, "tbl_contentinfo", "titleName");
  int has_content_id = has_contentinfo && appdb_table_has_column(db, "tbl_contentinfo", "contentId");
  int has_sort_priority = has_appbrowse && appdb_table_has_column(db, "tbl_appbrowse", "sortPriority");
  int has_appinfo    = appdb_table_exists(db, "tbl_appinfo");
  int has_play_time  = has_appbrowse && appdb_table_has_column(db, "tbl_appbrowse", "playTime");

  if (has_appbrowse && has_contentinfo && has_content_id && has_browse_name && has_content_name && has_sort_priority) {
    sql = "SELECT DISTINCT b.titleId, COALESCE(b.titleName,''), COALESCE(c.titleName,''), COALESCE(c.contentId,''), "
          "COALESCE(b.sortPriority,0) "
          "FROM tbl_appbrowse b LEFT JOIN tbl_contentinfo c ON c.titleId=b.titleId "
          "WHERE b.titleId IS NOT NULL AND b.titleId!='' AND substr(b.titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 5 DESC, 2 COLLATE NOCASE, 1;";
  } else if (has_appbrowse && has_contentinfo && has_content_id && has_browse_name && has_content_name) {
    sql = "SELECT DISTINCT b.titleId, COALESCE(b.titleName,''), COALESCE(c.titleName,''), COALESCE(c.contentId,''), 0 "
          "FROM tbl_appbrowse b LEFT JOIN tbl_contentinfo c ON c.titleId=b.titleId "
          "WHERE b.titleId IS NOT NULL AND b.titleId!='' AND substr(b.titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 2 COLLATE NOCASE, 1;";
  } else if (has_appbrowse && has_contentinfo && has_content_id && has_browse_name) {
    sql = "SELECT DISTINCT b.titleId, COALESCE(b.titleName,''), '', COALESCE(c.contentId,''), 0 "
          "FROM tbl_appbrowse b LEFT JOIN tbl_contentinfo c ON c.titleId=b.titleId "
          "WHERE b.titleId IS NOT NULL AND b.titleId!='' AND substr(b.titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 2 COLLATE NOCASE, 1;";
  } else if (has_appbrowse && has_browse_name) {
    sql = "SELECT DISTINCT titleId, COALESCE(titleName,''), '', '', 0 "
          "FROM tbl_appbrowse "
          "WHERE titleId IS NOT NULL AND titleId!='' AND substr(titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 2 COLLATE NOCASE, 1;";
  } else if (has_appbrowse) {
    sql = "SELECT DISTINCT titleId, '', '', '', 0 "
          "FROM tbl_appbrowse "
          "WHERE titleId IS NOT NULL AND titleId!='' AND substr(titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 1;";
  } else if (has_contentinfo && has_content_name && has_content_id) {
    sql = "SELECT DISTINCT titleId, '', COALESCE(titleName,''), COALESCE(contentId,''), 0 "
          "FROM tbl_contentinfo "
          "WHERE titleId IS NOT NULL AND titleId!='' AND substr(titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 3 COLLATE NOCASE, 1;";
  } else if (has_contentinfo && has_content_id) {
    sql = "SELECT DISTINCT titleId, '', '', COALESCE(contentId,''), 0 "
          "FROM tbl_contentinfo "
          "WHERE titleId IS NOT NULL AND titleId!='' AND substr(titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 1;";
  } else if (has_contentinfo && has_content_name) {
    sql = "SELECT DISTINCT titleId, '', COALESCE(titleName,''), '', 0 "
          "FROM tbl_contentinfo "
          "WHERE titleId IS NOT NULL AND titleId!='' AND substr(titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 3 COLLATE NOCASE, 1;";
  } else if (has_contentinfo) {
    sql = "SELECT DISTINCT titleId, '', '', '', 0 "
          "FROM tbl_contentinfo "
          "WHERE titleId IS NOT NULL AND titleId!='' AND substr(titleId,1,4) IN "
          "('CUSA','PPSA','ULUS','ULES','ULJS','ULKS','SLUS','SCUS','SLES','SCES','SLPS','SLPM','SCED','SLED','SCPS') "
          "ORDER BY 1;";
  } else {
    goto done;
  }

  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
    goto done;
  }

  int debug_names = 0;
  pthread_mutex_lock(&g_cfg_lock);
  debug_names = g_cfg.appdb_debug_names ? 1 : 0;
  pthread_mutex_unlock(&g_cfg_lock);

  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char *id = sqlite3_column_text(st, 0);
    if (!id || !id[0]) {
      continue;
    }
    char title_id[16];
    char norm_id[10];
    char title_name[256] = {0};
    char browse_name[256] = {0};
    char content_name[256] = {0};
    char content_id[96];
    const char *name_source = "fallback_title_id";
    if (!title_id_normalize((const char *)id, norm_id)) {
      continue;
    }
    snprintf(title_id, sizeof(title_id), "%s", norm_id);
    const unsigned char *bname = sqlite3_column_text(st, 1);
    const unsigned char *cname = sqlite3_column_text(st, 2);
    const unsigned char *cid   = sqlite3_column_text(st, 3);
    snprintf(browse_name, sizeof(browse_name), "%s", bname ? (const char *)bname : "");
    snprintf(content_name, sizeof(content_name), "%s", cname ? (const char *)cname : "");
    snprintf(content_id, sizeof(content_id), "%s", cid ? (const char *)cid : "");

    if (!cr_title_name_is_unresolved(title_id, browse_name)) {
      snprintf(title_name, sizeof(title_name), "%s", browse_name);
      name_source = "tbl_appbrowse";
    } else if (!cr_title_name_is_unresolved(title_id, content_name)) {
      snprintf(title_name, sizeof(title_name), "%s", content_name);
      name_source = "tbl_contentinfo";
    }

    if (cr_title_name_is_unresolved(title_id, title_name) && has_appinfo &&
        appdb_read_appinfo_name(db, title_id, title_name, sizeof(title_name))) {
      name_source = "tbl_appinfo";
    }
    if (cr_title_name_is_unresolved(title_id, title_name) &&
        read_param_value_by_title_id(title_id, "titleName", title_name, sizeof(title_name)) == 0 &&
        !cr_title_name_is_unresolved(title_id, title_name)) {
      name_source = "param_json";
    }
    if (cr_title_name_is_unresolved(title_id, title_name) &&
        title_lookup_cache_read(title_id, title_name, sizeof(title_name))) {
      name_source = "cache_lookup";
    }
    if (!content_id[0]) {
      read_param_value_by_title_id(title_id, "contentId", content_id, sizeof(content_id));
    }
    if (cr_title_name_is_unresolved(title_id, title_name)) {
      snprintf(title_name, sizeof(title_name), "%s", title_id);
      name_source = "fallback_title_id";
      if (debug_names) {
        cr_log("debug", "appdb", "unresolved name title=%s browseName=%s contentName=%s",
               title_id, browse_name, content_name);
      }
    } else {
      if (debug_names) {
        cr_log("debug", "appdb", "name title=%s source=%s value=\"%s\"",
               title_id, name_source, title_name);
      }
    }
    appdb_merge_game(entries, count, title_id, title_name, content_id, name_source);
  }

  rc = 0;

  /* The home-screen numbers come from the per-profile tables; the old columns are only a fallback. */
  if (*count > 0 && appdb_merge_activity(db, entries, *count) == 0) {
    /* Read native PS5 playtime from tbl_appbrowse.playTime (seconds, if column exists) */
    if (has_play_time && *count > 0) {
      sqlite3_stmt *pt_st = NULL;
      if (sqlite3_prepare_v2(db,
            "SELECT titleId, COALESCE(playTime,0) FROM tbl_appbrowse WHERE playTime > 0",
            -1, &pt_st, NULL) == SQLITE_OK) {
        while (sqlite3_step(pt_st) == SQLITE_ROW) {
          const unsigned char *pt_id = sqlite3_column_text(pt_st, 0);
          sqlite3_int64 pt_val = sqlite3_column_int64(pt_st, 1);
          if (!pt_id || !pt_id[0] || pt_val <= 0) continue;
          char pt_norm[10];
          if (!title_id_normalize((const char *)pt_id, pt_norm)) continue;
          for (size_t k = 0; k < *count; k++) {
            if (strcmp(entries[k].title_id, pt_norm) == 0) {
              entries[k].play_time_seconds = (uint64_t)pt_val;
              break;
            }
          }
        }
        sqlite3_finalize(pt_st);
      }
    }

    /* Read last access time from tbl_contentinfo.lastAccessTime */
    if (has_contentinfo && *count > 0) {
      sqlite3_stmt *la_st = NULL;
      if (sqlite3_prepare_v2(db,
            "SELECT titleId, CAST(strftime('%s', lastAccessTime) AS INTEGER) "
            "FROM tbl_contentinfo WHERE lastAccessTime IS NOT NULL AND lastAccessTime != ''",
            -1, &la_st, NULL) == SQLITE_OK) {
        while (sqlite3_step(la_st) == SQLITE_ROW) {
          const unsigned char *la_id = sqlite3_column_text(la_st, 0);
          sqlite3_int64 la_ts = sqlite3_column_int64(la_st, 1);
          if (!la_id || !la_id[0] || la_ts <= 0) continue;
          char la_norm[10];
          if (!title_id_normalize((const char *)la_id, la_norm)) continue;
          for (size_t k = 0; k < *count; k++) {
            if (strcmp(entries[k].title_id, la_norm) == 0) {
              entries[k].last_access_time = (uint64_t)la_ts;
              break;
            }
          }
        }
        sqlite3_finalize(la_st);
      }
    }


  }

done:
  if (st) {
    sqlite3_finalize(st);
  }
  if (db) {
    sqlite3_close(db);
  }
  pthread_mutex_unlock(&g_sqlite_lock);
  return rc;
}
#else
static int
appdb_collect_games_sqlite(game_entry_t *entries, size_t *count) {
  (void)entries;
  (void)count;
  return -1;
}
#endif

static int
game_entry_cmp(const void *a, const void *b) {
  const game_entry_t *ga = (const game_entry_t *)a;
  const game_entry_t *gb = (const game_entry_t *)b;
  int n = strcasecmp(ga->title_name, gb->title_name);
  if (n != 0) {
    return n;
  }
  return strcasecmp(ga->title_id, gb->title_id);
}

static volatile int g_appdb_fallback_warned = 0;
static char g_appdb_mode_str[32]   = "unknown";
static char g_appdb_reason_str[64] = "";
static size_t g_appdb_last_count   = 0;
static pthread_mutex_t g_appdb_mode_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t g_appdb_last_logged_db_count = (size_t)-1;
static size_t g_appdb_last_logged_merge_count = (size_t)-1;
static char g_appdb_last_logged_mode[32] = "";
static long long g_appdb_last_log_ms = 0;

int
appdb_collect_games(game_entry_t *entries, size_t *count) {
  *count = 0;
  size_t before_fallback = 0;
  int db_ok = appdb_collect_games_sqlite(entries, count) == 0 && *count > 0;
  before_fallback = *count;
  collect_games(entries, count);
  if (*count > 1) {
    qsort(entries, *count, sizeof(*entries), game_entry_cmp);
  }
  pthread_mutex_lock(&g_appdb_mode_lock);
  g_appdb_last_count = *count;
  if (db_ok) {
    g_appdb_fallback_warned = 0;
    snprintf(g_appdb_mode_str, sizeof(g_appdb_mode_str), "sqlite");
    size_t merge_count = (*count > before_fallback) ? (*count - before_fallback) : 0;
    snprintf(g_appdb_reason_str, sizeof(g_appdb_reason_str), "%zu from app.db +%zu folder",
             before_fallback, merge_count);
    long long ms = (long long)now_ms();
    int should_log = 0;
    if (g_appdb_last_log_ms == 0 ||
        strcmp(g_appdb_last_logged_mode, "sqlite") != 0 ||
        g_appdb_last_logged_db_count != before_fallback ||
        g_appdb_last_logged_merge_count != merge_count ||
        (ms - g_appdb_last_log_ms) > 60000) {
      should_log = 1;
    }
    if (should_log) {
      cr_log("info", "appdb", "loaded %zu titles from app.db (+%zu fallback merge)", before_fallback, merge_count);
      g_appdb_last_log_ms = ms;
      g_appdb_last_logged_db_count = before_fallback;
      g_appdb_last_logged_merge_count = merge_count;
      snprintf(g_appdb_last_logged_mode, sizeof(g_appdb_last_logged_mode), "%s", "sqlite");
    }
  } else {
    snprintf(g_appdb_mode_str, sizeof(g_appdb_mode_str), "folder_fallback");
    snprintf(g_appdb_reason_str, sizeof(g_appdb_reason_str), "app.db unavailable; %zu from folder scan", *count);
    if (!g_appdb_fallback_warned) {
      g_appdb_fallback_warned = 1;
#if CHEATRUNNER_HAVE_SQLITE_APPDB
      cr_log("warn", "appdb", "app.db query unavailable; using folder fallback (%zu titles)", *count);
#else
      cr_log("info", "appdb", "app.db scanner disabled; using folder fallback (%zu titles)", *count);
#endif
    }
  }
  pthread_mutex_unlock(&g_appdb_mode_lock);
  return 0;
}

#if CHEATRUNNER_HAVE_SQLITE_APPDB
static int
appdb_resolve_icon_path(const char *title_id, char *out, size_t out_size) {
  sqlite3 *db = NULL;
  int rc = 0;
  pthread_mutex_lock(&g_sqlite_lock);
  if (sqlite3_open_v2("/system_data/priv/mms/app.db", &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    pthread_mutex_unlock(&g_sqlite_lock);
    return 0;
  }
  if (!appdb_table_has_column(db, "tbl_contentinfo", "icon0Info")) {
    sqlite3_close(db);
    pthread_mutex_unlock(&g_sqlite_lock);
    return 0;
  }
  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT icon0Info FROM tbl_contentinfo WHERE titleId=?1 AND icon0Info IS NOT NULL AND icon0Info!='' LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
    sqlite3_close(db);
    pthread_mutex_unlock(&g_sqlite_lock);
    return 0;
  }
  sqlite3_bind_text(st, 1, title_id, -1, NULL);
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char *v = sqlite3_column_text(st, 0);
    if (v && v[0]) {
      const char *s = (const char *)v;
      const char *p = strstr(s, ".png");
      const char *cut = p;
      if (p) {
        const char *nxt;
        while ((nxt = strstr(p + 4, ".png")) != NULL) { cut = nxt; p = nxt; }
      }
      if (cut) {
        size_t n = (size_t)(cut - s) + 4;
        if (n >= out_size) n = out_size - 1;
        memcpy(out, s, n);
        out[n] = '\0';
        struct stat sst;
        if (stat(out, &sst) == 0 && sst.st_size > 0) rc = 1;
      }
    }
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  pthread_mutex_unlock(&g_sqlite_lock);
  return rc;
}
#endif

int
resolve_icon_path(const char *title_id, char *out, size_t out_size) {
#if CHEATRUNNER_HAVE_SQLITE_APPDB
  if (appdb_resolve_icon_path(title_id, out, out_size)) return 0;
#endif
  const char *candidates[] = {
      "/user/appmeta/%s/icon0.png",
      "/user/appmeta/%s/sce_sys/icon0.png",
      "/user/appmeta/external/%s/icon0.png",
      "/user/appmeta/external/%s/sce_sys/icon0.png",
      "/user/app/%s/sce_sys/icon0.png",
      "/user/patch/%s/sce_sys/icon0.png",
      "/mnt/ext0/user/app/%s/sce_sys/icon0.png",
      "/mnt/ext1/user/app/%s/sce_sys/icon0.png",
      "/mnt/ext0/user/patch/%s/sce_sys/icon0.png",
      "/mnt/ext1/user/patch/%s/sce_sys/icon0.png",
      NULL,
  };
  struct stat st;
  for (int i = 0; candidates[i]; i++) {
    snprintf(out, out_size, candidates[i], title_id);
    if (stat(out, &st) == 0 && st.st_size > 0) {
      return 0;
    }
  }
  out[0] = '\0';
  return -1;
}

int
resolve_pic0_path(const char *title_id, char *out, size_t out_size) {
  const char *candidates[] = {
      "/user/appmeta/%s/pic0.png",
      "/user/appmeta/%s/sce_sys/pic0.png",
      "/user/appmeta/external/%s/pic0.png",
      "/user/appmeta/external/%s/sce_sys/pic0.png",
      "/user/app/%s/sce_sys/pic0.png",
      "/user/patch/%s/sce_sys/pic0.png",
      "/mnt/ext0/user/app/%s/sce_sys/pic0.png",
      "/mnt/ext1/user/app/%s/sce_sys/pic0.png",
      "/mnt/ext0/user/patch/%s/sce_sys/pic0.png",
      "/mnt/ext1/user/patch/%s/sce_sys/pic0.png",
      NULL,
  };
  struct stat st;
  for (int i = 0; candidates[i]; i++) {
    snprintf(out, out_size, candidates[i], title_id);
    if (stat(out, &st) == 0 && st.st_size > 0) {
      return 0;
    }
  }
  out[0] = '\0';
  return -1;
}

int
cache_media_path(const char *title_id, int is_pic0, char *out, size_t out_size) {
  if (!title_id || !out || out_size < 64 || !is_valid_title_id(title_id)) {
    return -1;
  }
  snprintf(out, out_size, "%s/%s.png", is_pic0 ? CHEATRUNNER_CACHE_PIC0_DIR : CHEATRUNNER_CACHE_ICON_DIR, title_id);
  return 0;
}

int
cache_media_ensure(const char *title_id, int is_pic0, char *cached_path, size_t cached_size) {
  char src[512];
  struct stat st;
  if (cache_media_path(title_id, is_pic0, cached_path, cached_size) != 0) {
    return -1;
  }
  if (stat(cached_path, &st) == 0 && st.st_size > 0) {
    return 0;
  }
  if ((is_pic0 ? resolve_pic0_path(title_id, src, sizeof(src)) : resolve_icon_path(title_id, src, sizeof(src))) != 0) {
    return -1;
  }
  uint8_t *buf = NULL;
  size_t len = 0;
  if (read_file_bytes(src, &buf, &len) != 0 || !buf || len == 0) {
    free(buf);
    return -1;
  }
  int rc = write_file_atomic(cached_path, buf, len);
  free(buf);
  return rc;
}

void
appdb_diag_get(char *mode, size_t mode_size, char *reason, size_t reason_size, size_t *last_count) {
  pthread_mutex_lock(&g_appdb_mode_lock);
  if (mode && mode_size > 0) {
    snprintf(mode, mode_size, "%s", g_appdb_mode_str);
  }
  if (reason && reason_size > 0) {
    snprintf(reason, reason_size, "%s", g_appdb_reason_str);
  }
  if (last_count) {
    *last_count = g_appdb_last_count;
  }
  pthread_mutex_unlock(&g_appdb_mode_lock);
}
