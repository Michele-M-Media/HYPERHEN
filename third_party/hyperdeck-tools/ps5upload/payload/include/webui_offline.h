/* The console page's offline copy (client/src/console/offline/offlineCopy.ts): the service worker
 * (dist-console/sw.js) and the appcache manifest (dist-console/cache.appcache), embedded by xxd as
 * build/webui_offline.c (payload/Makefile). Served at GET /sw.js and GET /cache.appcache with their
 * own types and "Cache-Control: no-cache" (the browser checks them on every visit; a new build has
 * a new hash inside, which replaces the stored copy). Neither ever stores an /api/ reply. */
#ifndef PS5UPLOAD_WEBUI_OFFLINE_H
#define PS5UPLOAD_WEBUI_OFFLINE_H

#include <stddef.h>

/* The generated arrays (xxd -i naming). */
extern unsigned char webui_sw_js[];
extern unsigned int webui_sw_js_len;
extern unsigned char webui_cache_appcache[];
extern unsigned int webui_cache_appcache_len;

typedef struct {
    const char *ctype;
    const unsigned char *data;
    size_t len;
} webui_offline_file_t;

/* The file a request path names exactly ("/sw.js", "/cache.appcache"): 1 and *out filled, else 0. */
int webui_offline_find(const char *path, webui_offline_file_t *out);

#endif
