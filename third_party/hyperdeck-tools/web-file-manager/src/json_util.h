#pragma once

#include <stddef.h>

typedef struct strbuf {
  char *data;
  size_t len;
  size_t cap;
} strbuf_t;

int strbuf_reserve(strbuf_t *b, size_t extra);
int strbuf_append(strbuf_t *b, const char *s);
int strbuf_printf(strbuf_t *b, const char *fmt, ...);
int json_escape(strbuf_t *b, const char *s);
