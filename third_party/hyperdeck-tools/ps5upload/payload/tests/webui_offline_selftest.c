/* Host-side tests for GET /sw.js and GET /cache.appcache's lookup (webui_offline.c): exactly those
 * two paths resolve, each with the type its browser feature requires; nothing else (and never an
 * /api/ path) does. Build: see the root Makefile test-payload. */
#include <stdio.h>
#include <string.h>

#include "webui_offline.h"

static int failures = 0;

#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

/* Stand-ins for build/webui_offline.c. */
unsigned char webui_sw_js[] = "self.addEventListener";
unsigned int webui_sw_js_len = sizeof webui_sw_js - 1;
unsigned char webui_cache_appcache[] = "CACHE MANIFEST\n";
unsigned int webui_cache_appcache_len = sizeof webui_cache_appcache - 1;

int main(void) {
    webui_offline_file_t f;
    static const char *other[] = { "/", "/sw.js/", "/SW.JS", "/sw.json", "/api/sw.js", "/cache.appcache.bak",
                                   "/strings/sw.js", "/api/token", "", "sw.js" };
    size_t i;

    memset(&f, 0, sizeof f);
    CHECK(webui_offline_find("/sw.js", &f) == 1);
    CHECK(strcmp(f.ctype, "text/javascript; charset=utf-8") == 0);
    CHECK(f.data == webui_sw_js && f.len == webui_sw_js_len);

    memset(&f, 0, sizeof f);
    CHECK(webui_offline_find("/cache.appcache", &f) == 1);
    CHECK(strcmp(f.ctype, "text/cache-manifest; charset=utf-8") == 0);
    CHECK(f.data == webui_cache_appcache && f.len == webui_cache_appcache_len);

    for (i = 0; i < sizeof other / sizeof other[0]; i++) CHECK(webui_offline_find(other[i], &f) == 0);
    CHECK(webui_offline_find(NULL, &f) == 0);
    CHECK(webui_offline_find("/sw.js", NULL) == 0);

    if (failures) {
        fprintf(stderr, "webui_offline_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("webui_offline_selftest: ok\n");
    return 0;
}
