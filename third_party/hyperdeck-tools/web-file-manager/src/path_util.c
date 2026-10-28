#include "path_util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *
query_value(struct MHD_Connection *conn, const char *key) {
  const char *raw = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, key);
  char *value = raw ? strdup(raw) : NULL;

  if(value && !strcmp(key, "name")) {
    char *start = value;
    char *end;
    while(isspace((unsigned char)*start)) start++;
    end = start + strlen(start);
    while(end > start && isspace((unsigned char)end[-1])) end--;
    memmove(value, start, (size_t)(end - start));
    value[end - start] = 0;
  }
  return value;
}

char *
header_value(struct MHD_Connection *conn, const char *key) {
  const char *raw = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, key);
  return raw ? form_decode(raw, strlen(raw)) : NULL;
}

char *
request_value(struct MHD_Connection *conn, const char *header,
              const char *query) {
  char *value = header_value(conn, header);
  return value ? value : query_value(conn, query);
}

static int
hex_value(char c) {
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

char *
form_decode(const char *src, size_t len) {
  char *out = malloc(len + 1);
  size_t i;
  size_t j = 0;

  if(!out) {
    return NULL;
  }
  for(i = 0; i < len; i++) {
    if(src[i] == '+') {
      out[j++] = ' ';
    } else if(src[i] == '%' && i + 2 < len) {
      int hi = hex_value(src[i + 1]);
      int lo = hex_value(src[i + 2]);
      if(hi >= 0 && lo >= 0) {
        out[j++] = (char)((hi << 4) | lo);
        i += 2;
      } else {
        out[j++] = src[i];
      }
    } else {
      out[j++] = src[i];
    }
  }
  out[j] = 0;
  return out;
}

char *
body_form_value(const char *body, size_t body_size, const char *key) {
  size_t key_len = strlen(key);
  size_t pos = 0;

  while(body && pos < body_size) {
    size_t start = pos;
    size_t end;
    size_t eq;

    while(pos < body_size && body[pos] != '&') {
      pos++;
    }
    end = pos;
    if(pos < body_size && body[pos] == '&') {
      pos++;
    }
    eq = start;
    while(eq < end && body[eq] != '=') {
      eq++;
    }
    if(eq - start == key_len && !strncmp(body + start, key, key_len)) {
      size_t value_start = eq + (eq < end);
      size_t i;
      for(i = value_start; i < end; i++) {
        if(body[i] == '\0' ||
           (body[i] == '%' && i + 2 < end && body[i + 1] == '0' &&
            body[i + 2] == '0')) {
          errno = EINVAL;
          return NULL;
        }
      }
      return form_decode(body + value_start, end - value_start);
    }
  }
  return NULL;
}

void
free_paths(char **paths, size_t count) {
  size_t i;

  if(!paths) {
    return;
  }
  for(i = 0; i < count; i++) {
    free(paths[i]);
  }
  free(paths);
}

int
parse_paths(const char *raw, char ***out_paths, size_t *out_count) {
  char *copy;
  char *line;
  char *save;
  char **paths = NULL;
  size_t count = 0;
  size_t capacity = 0;
  const char *p;
  int in_line = 0;

  *out_paths = NULL;
  *out_count = 0;

  if(!raw || !raw[0]) {
    errno = EINVAL;
    return -1;
  }
  for(p = raw; *p; p++) {
    if(*p == '\n') {
      if(in_line) {
        capacity++;
        in_line = 0;
      }
      continue;
    }
    in_line = 1;
  }
  if(in_line) {
    capacity++;
  }
  if(!capacity) {
    errno = EINVAL;
    return -1;
  }

  if(!(paths = calloc(capacity, sizeof(char *))) ||
     !(copy = strdup(raw))) {
    free(paths);
    errno = ENOMEM;
    return -1;
  }

  for(line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    if(!line[0]) {
      continue;
    }
    if(!(paths[count] = fs_path_value(strdup(line)))) {
      free_paths(paths, count);
      free(copy);
      errno = ENOMEM;
      return -1;
    }
    count++;
  }

  free(copy);
  if(!count) {
    errno = EINVAL;
    return -1;
  }

  *out_paths = paths;
  *out_count = count;
  return 0;
}

const char *
path_basename(const char *path) {
  const char *end = path + strlen(path);
  const char *base;

  while(end > path && end[-1] == '/') {
    end--;
  }
  base = end;
  while(base > path && base[-1] != '/') {
    base--;
  }
  return base;
}

void
path_basename_copy(const char *path, char *out, size_t size) {
  const char *end = path + strlen(path);
  const char *base;
  size_t len;

  while(end > path && end[-1] == '/') {
    end--;
  }
  base = end;
  while(base > path && base[-1] != '/') {
    base--;
  }
  len = (size_t)(end - base);
  if(!len) {
    snprintf(out, size, "root");
    return;
  }
  if(len >= size) {
    len = size - 1;
  }
  memcpy(out, base, len);
  out[len] = 0;
}

int
path_dirname(const char *path, char *out, size_t size) {
  if(!strncmp(path, "smb://", 6)) {
    const char *share = strchr(path + 6, '/');
    const char *child = share ? strchr(share + 1, '/') : NULL;
    if(share && (!child || !child[1])) {
      if(size < 2) return -1;
      strcpy(out, "/");
      return 0;
    }
  }
  const char *base = path_basename(path);
  size_t len = (size_t)(base - path);

  while(len > 1 && path[len - 1] == '/') {
    len--;
  }
  if(!len) {
    len = 1;
  }
  if(len >= size) {
    return -1;
  }
  memcpy(out, path, len);
  out[len] = 0;
  return 0;
}

int
path_join(char *out, size_t size, const char *dir, const char *name) {
  int n;
  if(!dir || !name || !dir[0] || !name[0] || strchr(name, '/')) {
    errno = EINVAL;
    return -1;
  }
  n = snprintf(out, size, "%s%s%s", dir,
               (strcmp(dir, "/") && dir[strlen(dir) - 1] != '/') ? "/" : "",
               name);
  if(n < 0 || (size_t)n >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

int
relative_path_safe(const char *path) {
  const char *p = path;

  if(!path || !path[0] || path[0] == '/') {
    errno = EINVAL;
    return 0;
  }
  while(*p) {
    const char *start = p;
    size_t len;

    while(*p && *p != '/') {
      if(*p == '\\') {
        errno = EINVAL;
        return 0;
      }
      p++;
    }
    len = (size_t)(p - start);
    if(!len || (len == 1 && start[0] == '.') ||
       (len == 2 && start[0] == '.' && start[1] == '.')) {
      errno = EINVAL;
      return 0;
    }
    if(*p == '/') {
      p++;
    }
  }
  return 1;
}

int
path_join_relative(char *out, size_t size, const char *dir, const char *rel) {
  int n;

  if(!relative_path_safe(rel)) {
    return -1;
  }
  n = snprintf(out, size, "%s%s%s", dir,
               (strcmp(dir, "/") && dir[strlen(dir) - 1] != '/') ? "/" : "",
               rel);
  if(n < 0 || (size_t)n >= size) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

static int
utf8_decode_char(const char **ps, unsigned int *out) {
  const unsigned char *s = (const unsigned char *)*ps;
  unsigned char c = s[0];
  unsigned int cp;
  size_t n;
  size_t i;

  if(c < 0x80) {
    *out = c;
    *ps += 1;
    return 0;
  }
  if((c & 0xe0) == 0xc0) {
    cp = c & 0x1f;
    n = 2;
  } else if((c & 0xf0) == 0xe0) {
    cp = c & 0x0f;
    n = 3;
  } else if((c & 0xf8) == 0xf0) {
    cp = c & 0x07;
    n = 4;
  } else {
    return -1;
  }

  for(i = 1; i < n; i++) {
    unsigned char t = s[i];
    if(!t || (t & 0xc0) != 0x80) {
      return -1;
    }
    cp = (cp << 6) | (t & 0x3f);
  }
  if((n == 2 && cp < 0x80) ||
     (n == 3 && cp < 0x800) ||
     (n == 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
     (cp >= 0xd800 && cp <= 0xdfff)) {
    return -1;
  }

  *out = cp;
  *ps += n;
  return 0;
}

char *
fs_path_value(char *path) {
  const char *p;
  char *out;
  size_t len;
  size_t pos = 0;
  int has_byte_token = 0;

  if(!path) {
    return path;
  }
  for(p = path; *p;) {
    unsigned int cp;
    const char *next = p;

    if(utf8_decode_char(&next, &cp)) {
      return path;
    }
    if(cp >= 0x80 && cp <= 0xff) {
      has_byte_token = 1;
    } else if(cp > 0xff) {
      return path;
    }
    p = next;
  }
  if(!has_byte_token) {
    return path;
  }

  len = strlen(path);
  if(!(out = malloc(len + 1))) {
    return path;
  }
  for(p = path; *p;) {
    unsigned int cp;
    const char *next = p;

    if(utf8_decode_char(&next, &cp)) {
      free(out);
      return path;
    }
    out[pos++] = (char)(unsigned char)cp;
    p = next;
  }
  out[pos] = 0;
  free(path);
  return out;
}
