#include "json_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
strbuf_reserve(strbuf_t *b, size_t extra) {
  size_t need = b->len + extra + 1;
  char *tmp;
  size_t cap;

  if(need <= b->cap) {
    return 0;
  }

  cap = b->cap ? b->cap : 4096;
  while(cap < need) {
    cap *= 2;
  }

  if(!(tmp = realloc(b->data, cap))) {
    return -1;
  }

  b->data = tmp;
  b->cap = cap;
  return 0;
}

int
strbuf_append(strbuf_t *b, const char *s) {
  size_t n = strlen(s);
  if(strbuf_reserve(b, n)) {
    return -1;
  }
  memcpy(b->data + b->len, s, n + 1);
  b->len += n;
  return 0;
}

int
strbuf_printf(strbuf_t *b, const char *fmt, ...) {
  va_list ap;
  va_list cp;
  int n;

  va_start(ap, fmt);
  va_copy(cp, ap);
  n = vsnprintf(NULL, 0, fmt, cp);
  va_end(cp);
  if(n < 0 || strbuf_reserve(b, (size_t)n)) {
    va_end(ap);
    return -1;
  }

  vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
  va_end(ap);
  b->len += (size_t)n;
  return 0;
}

int
json_escape(strbuf_t *b, const char *s) {
  if(strbuf_append(b, "\"")) return -1;
  while(*s) {
    unsigned char c = (unsigned char)*s;
    switch(c) {
    case '"': if(strbuf_append(b, "\\\"")) return -1; s++; break;
    case '\\': if(strbuf_append(b, "\\\\")) return -1; s++; break;
    case '\b': if(strbuf_append(b, "\\b")) return -1; s++; break;
    case '\f': if(strbuf_append(b, "\\f")) return -1; s++; break;
    case '\n': if(strbuf_append(b, "\\n")) return -1; s++; break;
    case '\r': if(strbuf_append(b, "\\r")) return -1; s++; break;
    case '\t': if(strbuf_append(b, "\\t")) return -1; s++; break;
    default:
      if(c < 0x20 || c >= 0x80) {
        if(strbuf_printf(b, "\\u%04x", c)) return -1;
      } else {
        if(strbuf_reserve(b, 1)) return -1;
        b->data[b->len++] = (char)c;
        b->data[b->len] = 0;
      }
      s++;
      break;
    }
  }
  return strbuf_append(b, "\"");
}
