#include "sm_platform.h"
#include <json-c/json.h>
#include "sm_gameinfo.h"
#include "sm_limits.h"
#include "sm_path_state.h"

// --- Game Metadata Parsing ---
static bool extract_json_string(struct json_object *object, const char *key,
                                 char *out, size_t out_size) {
  struct json_object *value = NULL;
  if (!json_object_is_type(object, json_type_object) ||
      !json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_string))
    return false;
  size_t length = (size_t)json_object_get_string_len(value);
  const char *text = json_object_get_string(value);
  if (length == 0 || length >= out_size || memchr(text, '\0', length))
    return false;
  memcpy(out, text, length + 1u);
  return true;
}

static void extract_title_name(struct json_object *root, char *out) {
  struct json_object *localized = NULL;
  struct json_object *language = NULL;
  if (json_object_object_get_ex(root, "localizedParameters", &localized) &&
      json_object_is_type(localized, json_type_object)) {
    if (json_object_object_get_ex(localized, "en-US", &language) &&
        extract_json_string(language, "titleName", out, MAX_TITLE_NAME))
      return;
    char default_language[32];
    if (extract_json_string(localized, "defaultLanguage", default_language,
                             sizeof(default_language)) &&
        json_object_object_get_ex(localized, default_language, &language) &&
        extract_json_string(language, "titleName", out, MAX_TITLE_NAME))
      return;
  }
  if (extract_json_string(root, "titleName", out, MAX_TITLE_NAME))
    return;
  if (json_object_is_type(localized, json_type_object)) {
    json_object_object_foreach(localized, key, value) {
      (void)key;
      if (extract_json_string(value, "titleName", out, MAX_TITLE_NAME))
        return;
    }
  }
}

bool is_supported_game_title_id(const char *title_id) {
  if (!title_id)
    return false;
  for (size_t i = 0; i < 4u; ++i) {
    if (title_id[i] < 'A' || title_id[i] > 'Z')
      return false;
  }
  for (size_t i = 4u; i < 9u; ++i) {
    if (title_id[i] < '0' || title_id[i] > '9')
      return false;
  }
  return title_id[9] == '\0' && strncmp(title_id, "NPXS", 4u) != 0;
}

static bool resolve_game_param(const char *base_path, char path[MAX_PATH],
                               struct stat *st) {
  const char *names[] = {"param.json", "param.sfo"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    int written = snprintf(path, MAX_PATH, "%s/sce_sys/%s", base_path, names[i]);
    if (written < 0 || written >= MAX_PATH)
      return false;
    if (stat(path, st) == 0) {
      if (S_ISREG(st->st_mode))
        return true;
    } else if (errno != ENOENT && errno != ENOTDIR) {
      return false;
    }
  }
  return false;
}

static uint32_t read_sfo_u32(const unsigned char *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool parse_game_sfo(const unsigned char *data, size_t size,
                           char *out_id, char *out_name) {
  if (size < 20u || memcmp(data, "\0PSF", 4u) != 0)
    return false;
  size_t keys = read_sfo_u32(data + 8u);
  size_t values = read_sfo_u32(data + 12u);
  uint32_t count = read_sfo_u32(data + 16u);
  if (count > (size - 20u) / 16u || keys < 20u + (size_t)count * 16u ||
      keys >= values || values >= size)
    return false;

  for (uint32_t i = 0; i < count; ++i) {
    const unsigned char *entry = data + 20u + (size_t)i * 16u;
    size_t key = (size_t)entry[0] | ((size_t)entry[1] << 8);
    size_t length = read_sfo_u32(entry + 4u);
    size_t capacity = read_sfo_u32(entry + 8u);
    size_t value = read_sfo_u32(entry + 12u);
    if (key >= values - keys || value > size - values ||
        length > capacity || capacity > size - values - value)
      return false;
    if ((entry[3] != 0u && entry[3] != 2u) || entry[2] != 4u)
      continue;

    const char *name = (const char *)data + keys + key;
    size_t key_space = values - keys - key;
    char *out = NULL;
    size_t out_size = 0;
    if (key_space >= sizeof("TITLE_ID") &&
        memcmp(name, "TITLE_ID", sizeof("TITLE_ID")) == 0) {
      out = out_id;
      out_size = MAX_TITLE_ID;
    } else if (key_space >= sizeof("TITLE") &&
               memcmp(name, "TITLE", sizeof("TITLE")) == 0) {
      out = out_name;
      out_size = MAX_TITLE_NAME;
    }
    if (!out)
      continue;
    // Only scan what fits in the destination, even for repeated large fields.
    size_t scan_length = length < out_size ? length : out_size;
    const unsigned char *text = data + values + value;
    const unsigned char *end = memchr(text, '\0', scan_length);
    if ((!end && (entry[3] == 2u || length >= out_size)) || length == 0) {
      if (out == out_id)
        return false;
      continue;
    }
    size_t text_length = end ? (size_t)(end - text) : length;
    memcpy(out, data + values + value, text_length);
    out[text_length] = '\0';
  }
  return is_supported_game_title_id(out_id);
}

static bool game_param_stat_matches(const struct stat *current,
                                    const struct stat *expected) {
  return S_ISREG(current->st_mode) && current->st_dev == expected->st_dev &&
         current->st_ino == expected->st_ino &&
         current->st_size == expected->st_size &&
         current->st_mtime == expected->st_mtime &&
         current->st_mtim.tv_nsec == expected->st_mtim.tv_nsec &&
         current->st_ctime == expected->st_ctime &&
         current->st_ctim.tv_nsec == expected->st_ctim.tv_nsec;
}

bool get_game_info(const char *base_path, const struct stat *param_st,
                   char *out_id, char *out_name) {
  out_id[0] = '\0';
  out_name[0] = '\0';
  if (!S_ISREG(param_st->st_mode))
    return false;

  bool cached_valid = false;
  if (load_cached_game_info(base_path, param_st, out_id, out_name, &cached_valid))
    return cached_valid;

  if (param_st->st_size <= 0 || param_st->st_size > MAX_PARAM_JSON_SIZE) {
    store_cached_game_info(base_path, param_st, false, "", "");
    return false;
  }

  char path[MAX_PATH];
  struct stat current_st;
  if (!resolve_game_param(base_path, path, &current_st))
    return false;
  // O_NONBLOCK prevents a replaced FIFO from holding up the scanner/shutdown.
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    return false;
  if (fstat(fd, &current_st) != 0 ||
      !game_param_stat_matches(&current_st, param_st)) {
    close(fd);
    return false;
  }

  size_t len = (size_t)param_st->st_size;
  char *buf = (char *)malloc(len + 1);
  if (!buf) {
    close(fd);
    return false;
  }
  size_t offset = 0;
  while (offset < len) {
    ssize_t bytes = read(fd, buf + offset, len - offset);
    if (bytes < 0 && errno == EINTR)
      continue;
    if (bytes <= 0)
      break;
    offset += (size_t)bytes;
  }
  bool read_ok = offset == len && fstat(fd, &current_st) == 0 &&
                 game_param_stat_matches(&current_st, param_st);
  close(fd);
  if (!read_ok) {
    free(buf);
    return false;
  }
  buf[len] = '\0';

  bool valid;
  if (strcmp(strrchr(path, '/'), "/param.sfo") == 0) {
    valid = parse_game_sfo((const unsigned char *)buf, len, out_id, out_name);
  } else {
    struct json_tokener *tokener = json_tokener_new_ex(32);
    if (!tokener) {
      free(buf);
      return false;
    }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    struct json_object *root = json_tokener_parse_ex(tokener, buf, (int)len);
    size_t parsed = json_tokener_get_parse_end(tokener);
    while (parsed < len && isspace((unsigned char)buf[parsed]))
      parsed++;
    valid = json_tokener_get_error(tokener) == json_tokener_success &&
            parsed == len && json_object_is_type(root, json_type_object);
    json_tokener_free(tokener);
    if (valid) {
      valid = extract_json_string(root, "titleId", out_id, MAX_TITLE_ID) ||
              extract_json_string(root, "title_id", out_id, MAX_TITLE_ID);
      valid = valid && is_supported_game_title_id(out_id);
    }
    if (valid)
      extract_title_name(root, out_name);
    json_object_put(root);
  }
  if (valid) {
    if (out_name[0] == '\0')
      (void)strlcpy(out_name, out_id, MAX_TITLE_NAME);
  } else {
    out_id[0] = '\0';
    out_name[0] = '\0';
  }
  free(buf);

  store_cached_game_info(base_path, param_st, valid, out_id, out_name);
  return valid;
}

bool directory_has_game_param(const char *dir_path, struct stat *param_st_out) {
  char path[MAX_PATH];
  struct stat st;
  if (!resolve_game_param(dir_path, path, &st))
    return false;
  if (param_st_out)
    *param_st_out = st;
  return true;
}
