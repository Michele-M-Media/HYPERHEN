/* GET /sw.js and GET /cache.appcache: see include/webui_offline.h. */
#include "webui_offline.h"

#include <string.h>

int webui_offline_find(const char *path, webui_offline_file_t *out) {
    if (!path || !out) return 0;
    if (strcmp(path, "/sw.js") == 0) {
        /* A service worker must come as JavaScript or the browser refuses to register it. */
        out->ctype = "text/javascript; charset=utf-8";
        out->data = webui_sw_js;
        out->len = webui_sw_js_len;
        return 1;
    }
    if (strcmp(path, "/cache.appcache") == 0) {
        /* An appcache manifest is only honoured with this type. */
        out->ctype = "text/cache-manifest; charset=utf-8";
        out->data = webui_cache_appcache;
        out->len = webui_cache_appcache_len;
        return 1;
    }
    return 0;
}
