#include "pkg_info.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filemgr_internal.h"
#include "json_util.h"
#include "path_util.h"
#include "websrv.h"
#include "vfs.h"

#define PKG_CNT_MAGIC 0x7f434e54u
#define PKG_FIH_MAGIC 0x7f464948u
#define PKG_LIH_MAGIC 0x7f4c4948u
#define PKG_HEADER_SIZE 0xa0
#define PKG_ENTRY_SIZE 0x20
#define PKG_ENTRY_PARAM_SFO 0x1000u
#define PKG_ENTRY_ICON0_PNG 0x1200u
#define PKG_ENTRY_ICON0_LOCALIZED_LAST 0x121fu
#define PKG_ENTRY_PARAM_JSON 0x2000u
#define PKG_ENTRY_FLAG_ENCRYPTED 0x80000000u
#define PKG_ENTRY_MAX 65536u
#define PKG_PARAM_MAX (4u * 1024u * 1024u)
#define PKG_ICON_MAX (32u * 1024u * 1024u)
#define SFO_MAGIC 0x46535000u
#define SFO_ENTRY_SIZE 0x10
#define SFO_ENTRY_MAX 256u
#define JSON_TOKEN_MAX 4096u

typedef enum pkg_param_type {
  PKG_PARAM_SFO,
  PKG_PARAM_JSON,
} pkg_param_type_t;

typedef struct pkg_source {
  int fd;
  int borrowed;
  uint64_t size;
  uint32_t content_type;
  uint32_t content_flags;
  char content_id[37];
  uint64_t param_offset;
  uint32_t param_size;
  pkg_param_type_t param_type;
  int platform;
  uint64_t icon_offset;
  uint32_t icon_size;
} pkg_source_t;

typedef enum json_token_type {
  JSON_OBJECT,
  JSON_ARRAY,
  JSON_STRING,
  JSON_PRIMITIVE,
} json_token_type_t;

typedef struct json_token {
  json_token_type_t type;
  size_t start;
  size_t end;
  int parent;
} json_token_t;

static uint16_t
read_le16(const unsigned char *p) {
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t
read_le32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
read_le64(const unsigned char *p) {
  return (uint64_t)read_le32(p) | (uint64_t)read_le32(p + 4) << 32;
}

static uint32_t
read_be32(const unsigned char *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static int
range_valid(uint64_t offset, uint64_t size, uint64_t file_size) {
  return offset <= file_size && size <= file_size - offset;
}

static int
read_at(int fd, void *buffer, size_t size, uint64_t offset) {
  unsigned char *p = buffer;
  size_t done = 0;

  while(done < size) {
    ssize_t n = pread(fd, p + done, size - done, (off_t)(offset + done));
    if(n <= 0) {
      if(n < 0 && errno == EINTR) continue;
      return -1;
    }
    done += (size_t)n;
  }
  return 0;
}

static int
png_signature_valid(const unsigned char *data, size_t size) {
  static const unsigned char signature[] = "\x89PNG\r\n\x1a\n";

  return size >= sizeof(signature) - 1 &&
         !memcmp(data, signature, sizeof(signature) - 1);
}

static int
pkg_icon_is_png(const pkg_source_t *pkg, uint64_t offset, uint32_t size) {
  unsigned char signature[8];

  return size >= sizeof(signature) &&
         !read_at(pkg->fd, signature, sizeof(signature), offset) &&
         png_signature_valid(signature, sizeof(signature));
}

static void
pkg_source_close(pkg_source_t *pkg) {
  if(pkg->fd >= 0 && !pkg->borrowed) close(pkg->fd);
  pkg->fd = -1;
}

static int
pkg_source_open_fd(int fd, int borrowed, pkg_source_t *pkg) {
  unsigned char header[PKG_HEADER_SIZE];
  unsigned char *table = NULL;
  struct stat st;
  uint64_t container_offset = 0;
  uint64_t container_size;
  uint32_t magic;
  uint32_t entry_count;
  uint32_t table_offset;
  size_t table_size;
  int ret = -1;

  memset(pkg, 0, sizeof(*pkg));
  pkg->fd = fd;
  pkg->borrowed = borrowed;
  if(pkg->fd < 0 || fstat(pkg->fd, &st) ||
     !S_ISREG(st.st_mode) || st.st_size < PKG_HEADER_SIZE ||
     read_at(pkg->fd, header, sizeof(header), 0)) goto done;

  pkg->size = (uint64_t)st.st_size;
  magic = read_be32(header);
  pkg->platform = magic == PKG_FIH_MAGIC || magic == PKG_LIH_MAGIC ? 5 : 4;
  if(magic == PKG_FIH_MAGIC) {
    container_offset = read_le64(header + 0x58);
  } else if(magic == PKG_LIH_MAGIC) {
    container_offset = read_le64(header + 0x30);
  } else if(magic != PKG_CNT_MAGIC) {
    goto done;
  }
  if(container_offset) {
    if(!range_valid(container_offset, sizeof(header), pkg->size) ||
       read_at(pkg->fd, header, sizeof(header), container_offset) ||
       read_be32(header) != PKG_CNT_MAGIC) goto done;
  }
  container_size = pkg->size - container_offset;

  memcpy(pkg->content_id, header + 0x40, 36);
  pkg->content_id[36] = 0;
  pkg->content_type = read_be32(header + 0x74);
  pkg->content_flags = read_be32(header + 0x78);
  entry_count = read_be32(header + 0x10);
  table_offset = read_be32(header + 0x18);
  if(!entry_count || entry_count > PKG_ENTRY_MAX) goto done;
  table_size = (size_t)entry_count * PKG_ENTRY_SIZE;
  if(!range_valid(table_offset, table_size, container_size) ||
     !(table = malloc(table_size)) ||
     read_at(pkg->fd, table, table_size, container_offset + table_offset)) {
    goto done;
  }

  for(uint32_t i = 0; i < entry_count; i++) {
    const unsigned char *entry = table + (size_t)i * PKG_ENTRY_SIZE;
    uint32_t id = read_be32(entry);
    uint32_t flags1 = read_be32(entry + 0x08);
    uint32_t offset = read_be32(entry + 0x10);
    uint32_t size = read_be32(entry + 0x14);
    int encrypted = (flags1 & PKG_ENTRY_FLAG_ENCRYPTED) != 0;

    if(!range_valid(offset, size, container_size)) goto done;
    if(encrypted || !size) continue;
    if(id == PKG_ENTRY_PARAM_SFO && size <= PKG_PARAM_MAX &&
       (!pkg->param_size || pkg->param_type == PKG_PARAM_SFO)) {
      pkg->param_offset = container_offset + offset;
      pkg->param_size = size;
      pkg->param_type = PKG_PARAM_SFO;
    } else if(id == PKG_ENTRY_PARAM_JSON && size <= PKG_PARAM_MAX) {
      pkg->param_offset = container_offset + offset;
      pkg->param_size = size;
      pkg->param_type = PKG_PARAM_JSON;
      pkg->platform = 5;
    } else if(id >= PKG_ENTRY_ICON0_PNG &&
              id <= PKG_ENTRY_ICON0_LOCALIZED_LAST &&
              size <= PKG_ICON_MAX &&
              (id == PKG_ENTRY_ICON0_PNG || !pkg->icon_size) &&
              pkg_icon_is_png(pkg, container_offset + offset, size)) {
      /* Prefer icon0.png; otherwise keep the first valid localized icon. */
      pkg->icon_offset = container_offset + offset;
      pkg->icon_size = size;
    }
  }
  if(!pkg->param_size) goto done;
  ret = 0;

done:
  free(table);
  if(ret) {
    errno = EINVAL;
    pkg_source_close(pkg);
  }
  return ret;
}

static int
pkg_source_open(const char *path, pkg_source_t *pkg) {
  return pkg_source_open_fd(open(path, O_RDONLY), 0, pkg);
}

static int
append_sfo_fields(strbuf_t *json, const unsigned char *sfo, size_t size, pkg_details_t *details) {
  uint32_t key_offset;
  uint32_t value_offset;
  uint32_t count;
  uint64_t index_end;
  int first = 1;

  if(size < 20 || read_le32(sfo) != SFO_MAGIC) return -1;
  key_offset = read_le32(sfo + 8);
  value_offset = read_le32(sfo + 12);
  count = read_le32(sfo + 16);
  index_end = 20 + (uint64_t)count * SFO_ENTRY_SIZE;
  if(count > SFO_ENTRY_MAX || index_end > size || key_offset < index_end ||
     value_offset < key_offset || value_offset > size) return -1;

  strbuf_append(json, "[");
  for(uint32_t i = 0; i < count; i++) {
    const unsigned char *entry = sfo + 20 + (size_t)i * SFO_ENTRY_SIZE;
    uint16_t name_offset = read_le16(entry);
    uint16_t format = read_le16(entry + 2);
    uint32_t value_size = read_le32(entry + 4);
    uint32_t value_max = read_le32(entry + 8);
    uint32_t data_offset = read_le32(entry + 12);
    uint64_t key_pos = (uint64_t)key_offset + name_offset;
    uint64_t value_pos = (uint64_t)value_offset + data_offset;
    const unsigned char *key_end;
    char number[32];
    char *value = NULL;

    if(key_pos >= value_offset || value_pos > size ||
       (value_max && value_size > value_max)) continue;
    key_end = memchr(sfo + key_pos, 0, value_offset - (size_t)key_pos);
    if(!key_end) continue;
    if(format == 0x0204) {
      size_t length;
      if(!value_size || !range_valid(value_pos, value_size, size)) continue;
      length = strnlen((const char *)sfo + value_pos, value_size);
      if(!(value = malloc(length + 1))) continue;
      memcpy(value, sfo + value_pos, length);
      value[length] = 0;
    } else if(format == 0x0404 && range_valid(value_pos, 4, size)) {
      snprintf(number, sizeof(number), "%u", read_le32(sfo + value_pos));
      value = strdup(number);
    }
    if(!value) continue;
    if(details && !strcmp((const char *)sfo + key_pos, "TITLE") && strlen(value) < sizeof(details->title))
      strcpy(details->title, value);
    if(details && !strcmp((const char *)sfo + key_pos, "CONTENT_ID") && strlen(value) == 36)
      strcpy(details->content_id, value);
    if(!first) strbuf_append(json, ",");
    first = 0;
    strbuf_append(json, "{\"name\":");
    json_escape(json, (const char *)sfo + key_pos);
    strbuf_append(json, ",\"value\":");
    json_escape(json, value);
    strbuf_append(json, "}");
    free(value);
  }
  strbuf_append(json, "]");
  return 0;
}

static int
json_add_token(json_token_t *tokens, size_t *count, json_token_type_t type,
               size_t start, int parent) {
  if(*count >= JSON_TOKEN_MAX) return -1;
  tokens[*count] = (json_token_t){
    .type = type, .start = start, .end = 0, .parent = parent
  };
  return (int)(*count)++;
}

static int
parse_json_tokens(const unsigned char *data, size_t size, json_token_t *tokens,
                  size_t *token_count) {
  int parent = -1;
  size_t count = 0;

  for(size_t i = 0; i < size; i++) {
    unsigned char c = data[i];
    if(c == '{' || c == '[') {
      int index = json_add_token(tokens, &count,
                                 c == '{' ? JSON_OBJECT : JSON_ARRAY,
                                 i, parent);
      if(index < 0) return -1;
      parent = index;
    } else if(c == '}' || c == ']') {
      json_token_type_t type = c == '}' ? JSON_OBJECT : JSON_ARRAY;
      if(parent < 0 || tokens[parent].type != type) return -1;
      tokens[parent].end = i + 1;
      parent = tokens[parent].parent;
    } else if(c == '"') {
      int index = json_add_token(tokens, &count, JSON_STRING, i + 1, parent);
      if(index < 0) return -1;
      for(i++; i < size && data[i] != '"'; i++) {
        if(data[i] < 0x20) return -1;
        if(data[i] == '\\') {
          if(++i >= size || !strchr("\"\\/bfnrtu", data[i])) return -1;
          if(data[i] == 'u') {
            for(unsigned int n = 0; n < 4; n++) {
              if(++i >= size || !((data[i] >= '0' && data[i] <= '9') ||
                                  (data[i] >= 'a' && data[i] <= 'f') ||
                                  (data[i] >= 'A' && data[i] <= 'F'))) return -1;
            }
          }
        }
      }
      if(i >= size) return -1;
      tokens[index].end = i;
    } else if(c == ':' || c == ',' || c == ' ' || c == '\t' ||
              c == '\r' || c == '\n') {
      continue;
    } else {
      int index = json_add_token(tokens, &count, JSON_PRIMITIVE, i, parent);
      if(index < 0) return -1;
      while(i < size && data[i] != ',' && data[i] != ']' && data[i] != '}' &&
            data[i] != ' ' && data[i] != '\t' && data[i] != '\r' &&
            data[i] != '\n') i++;
      if(tokens[index].start == i) return -1;
      tokens[index].end = i;
      i--;
    }
  }
  if(parent >= 0 || !count || tokens[0].type != JSON_OBJECT ||
     !tokens[0].end) return -1;
  *token_count = count;
  return 0;
}

static int
json_token_equals(const unsigned char *data, const json_token_t *token,
                  const char *text) {
  size_t length = strlen(text);
  return token->type == JSON_STRING && token->end - token->start == length &&
         !memcmp(data + token->start, text, length);
}

static int
json_next_child(const json_token_t *tokens, size_t count, int parent,
                size_t start) {
  for(size_t i = start; i < count; i++) {
    if(tokens[i].parent == parent) return (int)i;
  }
  return -1;
}

static int
json_object_value(const unsigned char *data, const json_token_t *tokens,
                  size_t count, int object, const char *key) {
  int item = json_next_child(tokens, count, object, (size_t)object + 1);

  while(item >= 0) {
    int value = json_next_child(tokens, count, object, (size_t)item + 1);
    if(value < 0) return -1;
    if(json_token_equals(data, &tokens[item], key)) return value;
    item = json_next_child(tokens, count, object, (size_t)value + 1);
  }
  return -1;
}

static int
json_object_value_token(const unsigned char *data, const json_token_t *tokens,
                        size_t count, int object, const json_token_t *key) {
  int item = json_next_child(tokens, count, object, (size_t)object + 1);
  size_t key_size = key->end - key->start;

  while(item >= 0) {
    int value = json_next_child(tokens, count, object, (size_t)item + 1);
    if(value < 0) return -1;
    if(tokens[item].type == JSON_STRING &&
       tokens[item].end - tokens[item].start == key_size &&
       !memcmp(data + tokens[item].start, data + key->start, key_size)) {
      return value;
    }
    item = json_next_child(tokens, count, object, (size_t)value + 1);
  }
  return -1;
}

static void
append_json_token_string(strbuf_t *json, const unsigned char *data,
                         const json_token_t *token) {
  strbuf_append(json, "\"");
  strbuf_printf(json, "%.*s", (int)(token->end - token->start),
                data + token->start);
  strbuf_append(json, "\"");
}

static void
append_json_field(strbuf_t *json, const unsigned char *data,
                  const json_token_t *key, const json_token_t *value,
                  int *first) {
  if(!*first) strbuf_append(json, ",");
  *first = 0;
  strbuf_append(json, "{\"name\":");
  append_json_token_string(json, data, key);
  strbuf_append(json, ",\"value\":");
  append_json_token_string(json, data, value);
  strbuf_append(json, "}");
}

static void
append_named_json_field(strbuf_t *json, const char *name,
                        const unsigned char *data, const json_token_t *value,
                        int *first) {
  if(!*first) strbuf_append(json, ",");
  *first = 0;
  strbuf_append(json, "{\"name\":");
  json_escape(json, name);
  strbuf_append(json, ",\"value\":");
  append_json_token_string(json, data, value);
  strbuf_append(json, "}");
}

/* Decode JSON strings for native metadata, including UTF-16 surrogate pairs. */
static int
copy_json_string(char *out, size_t capacity, const unsigned char *data, const json_token_t *token) {
  if(token->type != JSON_STRING) return -1;
  size_t used = 0;
  for(size_t i = token->start; i < token->end; i++) {
    unsigned char bytes[4];
    size_t length = 1;
    bytes[0] = data[i];
    if(data[i] == '\\') {
      if(++i >= token->end) return -1;
      if(data[i] == 'u') {
        unsigned int cp = 0;
        for(int part = 0; part < 2; part++) {
          unsigned int unit = 0;
          for(int n = 0; n < 4; n++) {
            if(++i >= token->end) return -1;
            unsigned char c = data[i];
            if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return -1;
            unit = unit * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
          }
          if(!part && unit >= 0xd800 && unit <= 0xdbff) {
            cp = unit;
            if(i + 2 >= token->end || data[++i] != '\\' || data[++i] != 'u') return -1;
            continue;
          }
          if(part) {
            if(unit < 0xdc00 || unit > 0xdfff) return -1;
            cp = 0x10000 + ((cp - 0xd800) << 10) + unit - 0xdc00;
          } else {
            if(unit >= 0xdc00 && unit <= 0xdfff) return -1;
            cp = unit;
          }
          break;
        }
        if(!cp) return -1;
        if(cp < 0x80) bytes[0] = (unsigned char)cp;
        else if(cp < 0x800) {
          length = 2; bytes[0] = 0xc0 | (cp >> 6); bytes[1] = 0x80 | (cp & 63);
        } else if(cp < 0x10000) {
          length = 3; bytes[0] = 0xe0 | (cp >> 12); bytes[1] = 0x80 | ((cp >> 6) & 63); bytes[2] = 0x80 | (cp & 63);
        } else {
          length = 4; bytes[0] = 0xf0 | (cp >> 18); bytes[1] = 0x80 | ((cp >> 12) & 63);
          bytes[2] = 0x80 | ((cp >> 6) & 63); bytes[3] = 0x80 | (cp & 63);
        }
      } else {
        const char *keys = "bfnrt";
        const char *escape = strchr(keys, data[i]);
        bytes[0] = escape ? (unsigned char)"\b\f\n\r\t"[escape - keys] : data[i];
      }
    }
    if(used + length >= capacity) return -1;
    memcpy(out + used, bytes, length); used += length;
  }
  out[used] = 0;
  return 0;
}

static int
append_param_json_fields(strbuf_t *json, const unsigned char *data,
                         size_t size, pkg_details_t *details) {
  json_token_t *tokens = calloc(JSON_TOKEN_MAX, sizeof(*tokens));
  size_t count = 0;
  int first = 1;
  int localized;
  int title = -1;

  if(!tokens || parse_json_tokens(data, size, tokens, &count)) {
    free(tokens);
    return -1;
  }
  strbuf_append(json, "[");

  localized = json_object_value(data, tokens, count, 0, "localizedParameters");
  if(localized >= 0 && tokens[localized].type == JSON_OBJECT) {
    int language = json_object_value(data, tokens, count, localized,
                                     "defaultLanguage");
    int language_data = language >= 0 && tokens[language].type == JSON_STRING ?
      json_object_value_token(data, tokens, count, localized,
                              &tokens[language]) : -1;
    if(language_data >= 0 && tokens[language_data].type == JSON_OBJECT) {
      title = json_object_value(data, tokens, count, language_data,
                                "titleName");
    }
    if(title < 0) {
      int english = json_object_value(data, tokens, count, localized, "en-US");
      if(english >= 0 && tokens[english].type == JSON_OBJECT) {
        title = json_object_value(data, tokens, count, english, "titleName");
      }
    }
    if(title < 0) {
      int item = json_next_child(tokens, count, localized,
                                 (size_t)localized + 1);
      while(item >= 0 && title < 0) {
        int value = json_next_child(tokens, count, localized,
                                    (size_t)item + 1);
        if(value < 0) break;
        if(tokens[value].type == JSON_OBJECT) {
          title = json_object_value(data, tokens, count, value, "titleName");
        }
        item = json_next_child(tokens, count, localized, (size_t)value + 1);
      }
    }
  }
  if(title >= 0 && tokens[title].type == JSON_STRING) {
    append_named_json_field(json, "titleName", data, &tokens[title], &first);
    if(details && copy_json_string(details->title, sizeof(details->title), data, &tokens[title])) details->title[0] = 0;
  }

  for(int item = json_next_child(tokens, count, 0, 1); item >= 0;) {
    int value = json_next_child(tokens, count, 0, (size_t)item + 1);
    if(value < 0) break;
    if(tokens[item].type == JSON_STRING &&
       (tokens[value].type == JSON_STRING ||
        tokens[value].type == JSON_PRIMITIVE)) {
      append_json_field(json, data, &tokens[item], &tokens[value], &first);
    }
    item = json_next_child(tokens, count, 0, (size_t)value + 1);
  }
  if(details) {
    int id = json_object_value(data, tokens, count, 0, "contentId");
    char content_id[37];
    if(id >= 0 && !copy_json_string(content_id, sizeof(content_id), data, &tokens[id]) && strlen(content_id) == 36)
      strcpy(details->content_id, content_id);
    if(!details->title[0]) {
      int name = json_object_value(data, tokens, count, 0, "titleName");
      if(name >= 0 && copy_json_string(details->title, sizeof(details->title), data, &tokens[name])) details->title[0] = 0;
    }
  }
  strbuf_append(json, "]");
  free(tokens);
  return 0;
}

int
pkg_read_details(const char *path, pkg_details_t *details) {
  pkg_source_t pkg = {.fd = -1};
  strbuf_t fields = {0};
  unsigned char *param = NULL;
  int result = -1;
  memset(details, 0, sizeof(*details));
  if(pkg_source_open(path, &pkg)) return -1;
  if(!(param = malloc(pkg.param_size)) || read_at(pkg.fd, param, pkg.param_size, pkg.param_offset)) goto done;
  details->platform = pkg.platform;
  details->has_icon = pkg.icon_size != 0;
  memcpy(details->content_id, pkg.content_id, sizeof(details->content_id));
  result = pkg.param_type == PKG_PARAM_JSON ?
    append_param_json_fields(&fields, param, pkg.param_size, details) :
    append_sfo_fields(&fields, param, pkg.param_size, details);
  if(strlen(details->content_id) != 36 || strspn(details->content_id,
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != 36) details->content_id[0] = 0;
done:
  free(fields.data); free(param); pkg_source_close(&pkg);
  if(result) memset(details, 0, sizeof(*details));
  return result;
}

static enum MHD_Result
pkg_error(struct MHD_Connection *conn, const char *path) {
  return send_json_error_detail(conn, MHD_HTTP_BAD_REQUEST,
                                "could not read package information",
                                "pkg_info_failed", path);
}

enum MHD_Result
api_pkg_info(struct MHD_Connection *conn) {
  char *path = fs_path_value(query_value(conn, "path"));
  pkg_source_t pkg = {.fd = -1};
  unsigned char *param;
  strbuf_t json = {0};

  if(!path || pkg_source_open(path, &pkg)) {
    enum MHD_Result ret = pkg_error(conn, path);
    free(path);
    return ret;
  }
  if(!(param = malloc(pkg.param_size)) ||
     read_at(pkg.fd, param, pkg.param_size, pkg.param_offset)) {
    enum MHD_Result ret = pkg_error(conn, path);
    free(param);
    pkg_source_close(&pkg);
    free(path);
    return ret;
  }

  strbuf_printf(&json,
                "{\"ok\":true,\"size\":%llu,\"content_id\":",
                (unsigned long long)pkg.size);
  json_escape(&json, pkg.content_id);
  strbuf_printf(&json,
                ",\"platform\":\"%s\",\"content_type\":%u,"
                "\"content_flags\":%u,\"has_icon\":%s,\"fields\":",
                pkg.platform == 5 ? "PS5" : "PS4",
                pkg.content_type, pkg.content_flags,
                pkg.icon_size ? "true" : "false");
  if((pkg.param_type == PKG_PARAM_JSON &&
      append_param_json_fields(&json, param, pkg.param_size, NULL)) ||
     (pkg.param_type == PKG_PARAM_SFO &&
      append_sfo_fields(&json, param, pkg.param_size, NULL))) {
    enum MHD_Result ret;
    free(json.data);
    ret = pkg_error(conn, path);
    free(param);
    pkg_source_close(&pkg);
    free(path);
    return ret;
  }
  strbuf_append(&json, "}");

  free(param);
  pkg_source_close(&pkg);
  free(path);
  return send_buffer(conn, MHD_HTTP_OK, json.data, "application/json");
}

enum MHD_Result
pkg_icon_response_fd(struct MHD_Connection *conn, int fd) {
  pkg_source_t pkg = {.fd = -1};
  unsigned char *icon = NULL;
  struct MHD_Response *response;
  enum MHD_Result ret;

  if(pkg_source_open_fd(fd, 1, &pkg) || !pkg.icon_size ||
     !(icon = malloc(pkg.icon_size)) ||
     read_at(pkg.fd, icon, pkg.icon_size, pkg.icon_offset)) {
    free(icon);
    if(pkg.fd >= 0) pkg_source_close(&pkg);
    return send_json_error(conn, MHD_HTTP_NOT_FOUND, "package icon not found");
  }
  if(!png_signature_valid(icon, pkg.icon_size)) {
    free(icon);
    pkg_source_close(&pkg);
    return send_json_error(conn, MHD_HTTP_NOT_FOUND,
                           "package icon not found");
  }
  pkg_source_close(&pkg);

  response = MHD_create_response_from_buffer(pkg.icon_size, icon,
                                              MHD_RESPMEM_MUST_FREE);
  if(!response) {
    free(icon);
    return MHD_NO;
  }
  MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "image/png");
  ret = websrv_queue_response(conn, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return ret;
}

enum MHD_Result
pkg_icon_response(struct MHD_Connection *conn, const char *path) {
  int fd = path ? open(path, O_RDONLY) : -1;
  enum MHD_Result result = pkg_icon_response_fd(conn, fd);
  if(fd >= 0) close(fd);
  return result;
}

enum MHD_Result
api_pkg_icon(struct MHD_Connection *conn) {
  char *path = fs_path_value(query_value(conn, "path"));
  enum MHD_Result result = pkg_icon_response(conn, path);
  free(path);
  return result;
}
