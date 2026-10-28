#include <ctype.h>
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cr_json.h"
#include "cr_paths.h"
#include "cr_titles.h"

static const char *const CR_TITLE_PREFIXES[] = {
    "CUSA", "PPSA", "NPXS", "PCAS",
    "ULUS", "ULES", "ULJS", "ULKS",
    "SLUS", "SCUS", "SLES", "SCES",
    "SLPS", "SLPM", "SCED", "SLED", "SCPS",
    NULL};

static int
cr_title_has_supported_prefix(const char *id9) {
  if (!id9 || strlen(id9) != 9) {
    return 0;
  }
  for (int i = 0; CR_TITLE_PREFIXES[i]; i++) {
    if (!strncmp(id9, CR_TITLE_PREFIXES[i], 4)) {
      return 1;
    }
  }
  return 0;
}

int
is_valid_title_id(const char *title_id) {
  if (!title_id || strlen(title_id) != 9) {
    return 0;
  }
  if (!cr_title_has_supported_prefix(title_id)) {
    return 0;
  }
  for (size_t i = 4; i < 9; i++) {
    if (!isalnum((unsigned char)title_id[i])) {
      return 0;
    }
  }
  return 1;
}

int
is_game_title_id(const char *title_id) {
  if (!title_id || strlen(title_id) != 9) {
    return 0;
  }
  return strncmp(title_id, "PPSA", 4) == 0 || strncmp(title_id, "CUSA", 4) == 0;
}

int
cr_title_name_is_unresolved(const char *title_id, const char *name) {
  if (!name) {
    return 1;
  }
  char tmp_name[256];
  snprintf(tmp_name, sizeof(tmp_name), "%s", name);
  str_trim(tmp_name);
  if (!tmp_name[0]) {
    return 1;
  }
  if (title_id && title_id[0] && !strcasecmp(tmp_name, title_id)) {
    return 1;
  }
  char norm_name[10];
  if (cr_title_id_normalize(tmp_name, norm_name)) {
    return 1;
  }
  if (!strcasecmp(tmp_name, "Unknown title") || !strcasecmp(tmp_name, "Unknown")) {
    return 1;
  }
  return 0;
}

int
cr_title_id_normalize(const char *in, char out[10]) {
  if (!in || !out) {
    return 0;
  }
  char packed[16];
  size_t w = 0;
  for (const char *p = in; *p && w < sizeof(packed) - 1; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '-' || c == '_' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    if (!isalnum(c)) {
      return 0;
    }
    if (c >= 'a' && c <= 'z') {
      c = (unsigned char)(c - 'a' + 'A');
    }
    packed[w++] = (char)c;
    if (w > 9) {
      return 0;
    }
  }
  packed[w] = '\0';
  if (w != 9) {
    return 0;
  }
  if (!is_valid_title_id(packed)) {
    return 0;
  }
  snprintf(out, 10, "%s", packed);
  return 1;
}

int
title_id_normalize(const char *in, char out[10]) {
  return cr_title_id_normalize(in, out);
}

int
find_title_id_in_string(const char *s, char out[10]) {
  if (!s || !out) {
    return 0;
  }
  size_t n = strlen(s);
  if (n < 9) {
    return 0;
  }
  for (size_t i = 0; i + 9 <= n; i++) {
    char tmp[10];
    for (size_t j = 0; j < 9; j++) {
      char c = s[i + j];
      if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
      }
      tmp[j] = c;
    }
    tmp[9] = '\0';
    if (is_valid_title_id(tmp)) {
      snprintf(out, 10, "%s", tmp);
      return 1;
    }
  }
  return 0;
}

int
extract_title_id_prefix(const char *filename, char *out, size_t out_size) {
  if (!filename || !out || out_size < 10) {
    return 0;
  }
  char tmp[10];
  if (title_id_normalize(filename, tmp) || find_title_id_in_string(filename, tmp)) {
    snprintf(out, out_size, "%s", tmp);
    return 1;
  }
  return 0;
}

int
case_contains(const char *haystack, const char *needle) {
  if (!haystack || !needle || !needle[0]) {
    return 0;
  }
  size_t hn = strlen(haystack);
  size_t nn = strlen(needle);
  if (nn > hn) {
    return 0;
  }
  for (size_t i = 0; i + nn <= hn; i++) {
    if (strncasecmp(haystack + i, needle, nn) == 0) {
      return 1;
    }
  }
  return 0;
}

static uint16_t
read_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t
read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int
sfo_read_string_value(const char *path, const char *key, char *out, size_t out_size) {
  uint8_t *buf = NULL;
  size_t len = 0;
  if (!path || !key || !out || out_size < 2 || read_file_bytes(path, &buf, &len) != 0) {
    return 0;
  }
  out[0] = '\0';
  if (len < 20 || memcmp(buf, "\0PSF", 4) != 0) {
    free(buf);
    return 0;
  }

  uint32_t key_table = read_le32(buf + 8);
  uint32_t data_table = read_le32(buf + 12);
  uint32_t count = read_le32(buf + 16);
  if (key_table >= len || data_table >= len || count > 1024 || 20 + count * 16 > len) {
    free(buf);
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    const uint8_t *entry = buf + 20 + i * 16;
    uint16_t key_off = read_le16(entry + 0);
    uint32_t data_len = read_le32(entry + 4);
    uint32_t data_off = read_le32(entry + 12);
    if (key_table + key_off >= len || data_table + data_off >= len) {
      continue;
    }
    const char *k = (const char *)(buf + key_table + key_off);
    size_t kmax = len - (key_table + key_off);
    if (memchr(k, '\0', kmax) == NULL) {
      continue;
    }
    if (strcmp(k, key) != 0) {
      continue;
    }
    size_t avail = len - (data_table + data_off);
    size_t n = data_len;
    if (n > avail) {
      n = avail;
    }
    while (n > 0 && buf[data_table + data_off + n - 1] == '\0') {
      n--;
    }
    if (n == 0) {
      free(buf);
      return 0;
    }
    if (n >= out_size) {
      n = out_size - 1;
    }
    memcpy(out, buf + data_table + data_off, n);
    out[n] = '\0';
    free(buf);
    return out[0] != '\0';
  }
  free(buf);
  return 0;
}

static int
sfo_read_value_for_key(const char *path, const char *param_key, char *out, size_t out_size) {
  const char *keys[4] = {0};
  if (!strcmp(param_key, "titleName")) {
    keys[0] = "TITLE";
  } else if (!strcmp(param_key, "titleId")) {
    keys[0] = "TITLE_ID";
  } else if (!strcmp(param_key, "contentId")) {
    keys[0] = "CONTENT_ID";
  } else if (!strcmp(param_key, "appVersion")) {
    keys[0] = "APP_VER";
    keys[1] = "VERSION";
  } else if (!strcmp(param_key, "contentVersion")) {
    keys[0] = "APP_VER";  /* game version; VERSION is often SFO schema version */
    keys[1] = "VERSION";
  } else {
    keys[0] = param_key;
  }
  for (int i = 0; keys[i]; i++) {
    if (sfo_read_string_value(path, keys[i], out, out_size)) {
      return 1;
    }
  }
  return 0;
}

static int
read_param_json_value(const char *json, const char *key, char *out, size_t out_size) {
  if (!json || !key || !out || out_size < 2) {
    return 0;
  }
  out[0] = '\0';
  cJSON *root = cJSON_Parse(json);
  if (root) {
    int ok = 0;
    if (!strcmp(key, "titleName")) {
      ok = cjson_read_localized_title(root, out, out_size);
    } else {
      cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
      ok = cjson_copy_string(v, out, out_size);
      if (!ok) {
        ok = cjson_find_string_recursive(root, key, out, out_size, 0);
      }
      /* PS5 param.json stores game version as "applicationVersion" */
      if (!ok && (!strcmp(key, "contentVersion") || !strcmp(key, "appVersion"))) {
        cJSON *av = cJSON_GetObjectItemCaseSensitive(root, "applicationVersion");
        ok = cjson_copy_string(av, out, out_size);
        if (!ok) ok = cjson_find_string_recursive(root, "applicationVersion", out, out_size, 0);
      }
    }
    cJSON_Delete(root);
    if (ok) {
      return 1;
    }
  }
  return json_extract_string(json, key, out, out_size);
}

const char *
platform_for_title_id(const char *title_id) {
  if (!title_id) {
    return "APP";
  }
  if (!strncmp(title_id, "PPSA", 4)) {
    return "PS5";
  }
  if (!strncmp(title_id, "CUSA", 4)) {
    return "PS4";
  }
  return "APP";
}

int
cr_title_is_known_media_app(const char *title_id, const char *name) {
  static const char * const known_ppsa[] = {
    "PPSA01650", /* YouTube */
    "PPSA01615", /* Netflix */
    "PPSA05688", /* Spotify */
    NULL
  };
  if (title_id) {
    for (int i = 0; known_ppsa[i]; i++) {
      if (!strcmp(title_id, known_ppsa[i])) return 1;
    }
  }
  if (!name || !name[0]) return 0;
  static const char * const keywords[] = {
    "youtube", "netflix", "spotify", "twitch", "disney", "prime video",
    "apple tv", "media player", "crunchyroll", "hulu", "plex", "max",
    "tubi", "dazn", NULL
  };
  char lower[256];
  size_t n = strlen(name);
  if (n >= sizeof(lower)) n = sizeof(lower) - 1;
  for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)name[i]);
  lower[n] = '\0';
  for (int i = 0; keywords[i]; i++) {
    if (strstr(lower, keywords[i])) return 1;
  }
  return 0;
}

int
read_param_value_from_dir(const char *dir, const char *key, char *out, size_t out_size) {
  if (!dir || !key || !out || out_size < 2) return -1;
  char path[512];
  out[0] = '\0';
  /* Try SFO */
  snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", dir);
  if (sfo_read_value_for_key(path, key, out, out_size) && out[0]) return 0;
  out[0] = '\0';
  /* Try JSON (PS5 native games store metadata as param.json) */
  snprintf(path, sizeof(path), "%s/sce_sys/param.json", dir);
  char *txt = NULL;
  if (read_file_text(path, &txt) == 0) {
    int ok = read_param_json_value(txt, key, out, out_size);
    free(txt);
    if (ok && out[0]) return 0;
  }
  out[0] = '\0';
  return -1;
}

/* Finds the content ID matching title_id (e.g. UP9000-CUSA42556_00-HASH) to derive the sandbox mount path. */
static int
appmeta_scan_for_title(const char *title_id, const char *key, char *out, size_t out_size) {
  static const char *const bases[] = {
    "/user/appmeta",
    "/mnt/ext0/user/appmeta",
    "/mnt/ext1/user/appmeta",
    NULL
  };
  out[0] = '\0';
  for (int b = 0; bases[b]; b++) {
    DIR *dp = opendir(bases[b]);
    if (!dp) continue;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
      /* Content IDs always contain the title ID as a substring */
      if (!strstr(ent->d_name, title_id)) continue;
      if (!is_safe_filename(ent->d_name)) continue;

      /* Try the sandbox mount (patch0 = current installed update; app0 = base) */
      char dir[256];
      const char *const subs[] = { "patch0", "app0", NULL };
      for (int si = 0; subs[si]; si++) {
        snprintf(dir, sizeof(dir), "/mnt/sandbox/%s_000/%s", ent->d_name, subs[si]);
        if (read_param_value_from_dir(dir, key, out, out_size) == 0 && out[0]) {
          closedir(dp);
          return 0;
        }
      }
      /* Also try the appmeta entry itself */
      snprintf(dir, sizeof(dir), "%s/%s", bases[b], ent->d_name);
      if (read_param_value_from_dir(dir, key, out, out_size) == 0 && out[0]) {
        closedir(dp);
        return 0;
      }
    }
    closedir(dp);
  }
  return -1;
}

int
read_param_value_from_appmeta(const char *title_id, const char *key, char *out, size_t out_size) {
  if (!title_id || !key || !out || out_size < 2) return -1;
  out[0] = '\0';
  return appmeta_scan_for_title(title_id, key, out, out_size);
}

int
read_param_value_by_title_id(const char *title_id, const char *key, char *out, size_t out_size) {
  const char *json_candidates[] = {
      /* system-wide paths, always accessible on PS5 */
      "/system_data/priv/appmeta/%s/param.json",
      "/system_data/priv/appmeta/external/%s/param.json",
      /* standard user paths */
      "/user/appmeta/%s/param.json",
      "/user/appmeta/%s/sce_sys/param.json",
      "/user/app/%s/sce_sys/param.json",
      "/system_ex/app/%s/sce_sys/param.json",   /* Folders mounted with nullfs (homebrew) */
      NULL,
  };
  const char *sfo_candidates[] = {
      /* system-wide paths, always accessible on PS5 */
      "/system_data/priv/appmeta/%s/param.sfo",
      "/system_data/priv/appmeta/external/%s/param.sfo",
      /* standard paths */
      "/user/patch/%s/sce_sys/param.sfo",           /* patch/update before base app */
      "/user/appmeta/%s/param.sfo",
      "/user/appmeta/%s/sce_sys/param.sfo",
      "/user/appmeta/external/%s/param.sfo",
      "/user/appmeta/external/%s/sce_sys/param.sfo",
      "/user/app/%s/sce_sys/param.sfo",
      "/system_ex/app/%s/sce_sys/param.sfo",
      "/mnt/ext0/user/patch/%s/sce_sys/param.sfo",
      "/mnt/ext1/user/patch/%s/sce_sys/param.sfo",
      "/mnt/ext0/user/app/%s/sce_sys/param.sfo",
      "/mnt/ext1/user/app/%s/sce_sys/param.sfo",
      "/mnt/ext0/user/appmeta/%s/param.sfo",
      "/mnt/ext1/user/appmeta/%s/param.sfo",
      "/mnt/ext0/user/appmeta/%s/sce_sys/param.sfo",
      "/mnt/ext1/user/appmeta/%s/sce_sys/param.sfo",
      "/mnt/ext0/user/appmeta/external/%s/param.sfo",
      "/mnt/ext1/user/appmeta/external/%s/param.sfo",
      "/mnt/ext0/user/appmeta/external/%s/sce_sys/param.sfo",
      "/mnt/ext1/user/appmeta/external/%s/sce_sys/param.sfo",
      NULL,
  };
  char path[512];
  char *json = NULL;
  out[0] = '\0';

  for (int i = 0; json_candidates[i]; i++) {
    snprintf(path, sizeof(path), json_candidates[i], title_id);
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > (1024 * 1024)) {
      continue;
    }
    if (read_file_text(path, &json) != 0) {
      continue;
    }
    int ok = read_param_json_value(json, key, out, out_size);
    free(json);
    json = NULL;
    if (ok && !strcmp(key, "titleName") && cr_title_name_is_unresolved(title_id, out)) {
      ok = 0;
      out[0] = '\0';
    }
    if (ok) {
      return 0;
    }
  }

  for (int i = 0; sfo_candidates[i]; i++) {
    snprintf(path, sizeof(path), sfo_candidates[i], title_id);
    if (sfo_read_value_for_key(path, key, out, out_size)) {
      return 0;
    }
  }
  return -1;
}

int
read_param_value_from_sfo(const char *sfo_path, const char *key, char *out, size_t out_size) {
  if (!sfo_path || !key || !out || out_size < 2) return -1;
  out[0] = '\0';
  return sfo_read_value_for_key(sfo_path, key, out, out_size) ? 0 : -1;
}

int
load_title_meta(const char *dir_path, const char *fallback_id, char *title_id, size_t id_size,
                char *title_name, size_t name_size) {
  const char *json_candidates[] = {"param.json", "sce_sys/param.json", NULL};
  const char *sfo_candidates[] = {"param.sfo", "sce_sys/param.sfo", NULL};
  char path[512];
  char *json = NULL;
  title_id[0] = '\0';
  title_name[0] = '\0';

  for (int i = 0; json_candidates[i]; i++) {
    snprintf(path, sizeof(path), "%s/%s", dir_path, json_candidates[i]);
    if (read_file_text(path, &json) != 0) {
      continue;
    }
    read_param_json_value(json, "titleId", title_id, id_size);
    read_param_json_value(json, "titleName", title_name, name_size);
    free(json);
    json = NULL;
    if (title_id[0] && title_name[0]) {
      break;
    }
  }

  for (int i = 0; sfo_candidates[i] && (!title_id[0] || !title_name[0]); i++) {
    snprintf(path, sizeof(path), "%s/%s", dir_path, sfo_candidates[i]);
    if (!title_id[0]) {
      sfo_read_value_for_key(path, "titleId", title_id, id_size);
    }
    if (!title_name[0]) {
      sfo_read_value_for_key(path, "titleName", title_name, name_size);
    }
  }

  if (!title_id[0] && fallback_id) {
    char norm_fallback[10];
    if (cr_title_id_normalize(fallback_id, norm_fallback)) {
      snprintf(title_id, id_size, "%s", norm_fallback);
    }
  }
  if (!title_name[0]) {
    snprintf(title_name, name_size, "%s", title_id[0] ? title_id : "Unknown title");
  }
  return title_id[0] ? 0 : -1;
}
