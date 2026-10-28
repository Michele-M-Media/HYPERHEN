#include "mime.h"

#include <strings.h>

typedef struct mime_entry {
  const char *ext;
  const char *mime;
} mime_entry_t;

static const mime_entry_t g_mimes[] = {
  {"bmp", "image/bmp"},
  {"css", "text/css"},
  {"elf", "application/octet-stream"},
  {"gif", "image/gif"},
  {"html", "text/html"},
  {"jpeg", "image/jpeg"},
  {"jpg", "image/jpeg"},
  {"js", "application/javascript"},
  {"json", "application/json"},
  {"log", "text/plain"},
  {"md", "text/plain"},
  {"png", "image/png"},
  {"txt", "text/plain"},
  {"xml", "text/xml"},
  {"webp", "image/webp"},
  {"zip", "application/zip"},
};

const char *
mime_get_type(const char *filename) {
  const char *ext = 0;
  size_t i;

  for(const char *p = filename; *p; p++) {
    if(*p == '.') {
      ext = p + 1;
    }
  }
  if(!ext || !*ext) {
    return "application/octet-stream";
  }

  for(i = 0; i < sizeof(g_mimes) / sizeof(g_mimes[0]); i++) {
    if(!strcasecmp(ext, g_mimes[i].ext)) {
      return g_mimes[i].mime;
    }
  }
  return "application/octet-stream";
}
