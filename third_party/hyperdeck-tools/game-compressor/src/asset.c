/*
 * File Explorer - embedded static assets.
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "asset.h"
#include "websrv.h"


#define PAGE_404 \
  "<!doctype html><html><head><title>Not found</title></head>" \
  "<body>Not found</body></html>"


typedef struct asset {
  const char   *path;
  const char   *mime;
  void         *data;
  size_t        size;
  struct asset *next;
} asset_t;


static asset_t *g_asset_head = NULL;


static void
asset_normalize_path(const char *url, char *path, size_t path_size) {
  char *ptr = path;
  char *end = path + path_size - 1;

  if(path_size == 0) return;

  for(size_t i = 0; url && url[i] && ptr < end; i++) {
    if(url[i] == '/' && url[i + 1] == '/') continue;
    *ptr++ = url[i];
  }
  *ptr = 0;
}


void
asset_register(const char *path, void *data, size_t size, const char *mime) {
  asset_t *a = calloc(1, sizeof(*a));
  if(!a) return;

  a->path = path;
  a->mime = mime;
  a->data = data;
  a->size = size;
  a->next = g_asset_head;
  g_asset_head = a;
}


int
asset_request(const http_request_t *req, const char *url) {
  char path[PATH_MAX];

  asset_normalize_path(url, path, sizeof(path));

  for(asset_t *a = g_asset_head; a; a = a->next) {
    if(!strcmp(path, a->path)) {
      return websrv_send(req->fd, 200, a->mime ? a->mime : "application/octet-stream",
                         a->data, a->size);
    }
  }

  return websrv_send(req->fd, 404, "text/html", PAGE_404, strlen(PAGE_404));
}
