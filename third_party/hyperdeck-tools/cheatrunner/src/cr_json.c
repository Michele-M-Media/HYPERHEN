#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cr_json.h"

char *
json_escape(const char *src) {
  if (!src) {
    return strdup("");
  }
  size_t len = strlen(src);
  char *out = malloc((len * 6) + 1);
  char *w = out;
  if (!out) {
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)src[i];
    switch (c) {
    case '"':
      *w++ = '\\';
      *w++ = '"';
      break;
    case '\\':
      *w++ = '\\';
      *w++ = '\\';
      break;
    case '\b':
      *w++ = '\\';
      *w++ = 'b';
      break;
    case '\f':
      *w++ = '\\';
      *w++ = 'f';
      break;
    case '\n':
      *w++ = '\\';
      *w++ = 'n';
      break;
    case '\r':
      *w++ = '\\';
      *w++ = 'r';
      break;
    case '\t':
      *w++ = '\\';
      *w++ = 't';
      break;
    default:
      if (c < 0x20) {
        w += sprintf(w, "\\u%04x", c);
      } else {
        *w++ = (char)c;
      }
      break;
    }
  }
  *w = '\0';
  return out;
}

int
json_extract_string(const char *json, const char *key, char *out, size_t out_size) {
  if (!json || !key || !out || out_size < 2) {
    return 0;
  }
  const char *hit = strstr(json, key);
  if (!hit) {
    return 0;
  }
  const char *colon = strchr(hit, ':');
  if (!colon) {
    return 0;
  }
  const char *q = strchr(colon, '"');
  if (!q) {
    return 0;
  }
  q++;
  size_t i = 0;
  while (*q && *q != '"' && i + 1 < out_size) {
    if (*q == '\\' && q[1]) {
      q++;
    }
    out[i++] = *q++;
  }
  out[i] = '\0';
  return i > 0;
}

int
cjson_copy_string(cJSON *item, char *out, size_t out_size) {
  if (!item || !out || out_size < 2 || !cJSON_IsString(item) || !item->valuestring) {
    return 0;
  }
  snprintf(out, out_size, "%s", item->valuestring);
  return out[0] != '\0';
}

int
cjson_find_string_recursive(cJSON *node, const char *key, char *out, size_t out_size, int depth) {
  if (!node || !key || !out || out_size < 2 || depth > 8) {
    return 0;
  }
  if (cJSON_IsObject(node)) {
    cJSON *direct = cJSON_GetObjectItemCaseSensitive(node, key);
    if (cjson_copy_string(direct, out, out_size)) {
      return 1;
    }
    for (cJSON *child = node->child; child; child = child->next) {
      if (cjson_find_string_recursive(child, key, out, out_size, depth + 1)) {
        return 1;
      }
    }
  } else if (cJSON_IsArray(node)) {
    for (cJSON *child = node->child; child; child = child->next) {
      if (cjson_find_string_recursive(child, key, out, out_size, depth + 1)) {
        return 1;
      }
    }
  }
  return 0;
}

int
cjson_read_localized_title(cJSON *root, char *out, size_t out_size) {
  if (!root || !out || out_size < 2) {
    return 0;
  }
  out[0] = '\0';

  if (cjson_copy_string(cJSON_GetObjectItemCaseSensitive(root, "titleName"), out, out_size) ||
      cjson_copy_string(cJSON_GetObjectItemCaseSensitive(root, "name"), out, out_size) ||
      cjson_copy_string(cJSON_GetObjectItemCaseSensitive(root, "TITLE"), out, out_size)) {
    return 1;
  }

  cJSON *localized = cJSON_GetObjectItemCaseSensitive(root, "localizedParameters");
  if (cJSON_IsObject(localized)) {
    char lang[32] = "";
    cjson_copy_string(cJSON_GetObjectItemCaseSensitive(root, "defaultLanguage"), lang, sizeof(lang));
    if (lang[0]) {
      cJSON *entry = cJSON_GetObjectItemCaseSensitive(localized, lang);
      if (cJSON_IsObject(entry) && cjson_find_string_recursive(entry, "titleName", out, out_size, 0)) {
        return 1;
      }
    }

    const char *langs[] = {
        "en-US", "en", "ja-JP", "ja", "pt-BR", "pt-PT", "es-ES", "es", "fr-FR", "fr",
        "de-DE", "de", "it-IT", "it", "ko-KR", "ko", "zh-CN", "zh-TW", NULL,
    };
    for (int i = 0; langs[i]; i++) {
      cJSON *entry = cJSON_GetObjectItemCaseSensitive(localized, langs[i]);
      if (cJSON_IsObject(entry) && cjson_find_string_recursive(entry, "titleName", out, out_size, 0)) {
        return 1;
      }
    }
    for (cJSON *entry = localized->child; entry; entry = entry->next) {
      if (cJSON_IsObject(entry) && cjson_find_string_recursive(entry, "titleName", out, out_size, 0)) {
        return 1;
      }
    }
  }

  return cjson_find_string_recursive(root, "titleName", out, out_size, 0);
}