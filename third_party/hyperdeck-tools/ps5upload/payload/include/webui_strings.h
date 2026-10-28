/* The console page's translations, one gzip'd JSON per language except English (which is inside
 * the page): client/dist-console/strings/<lang>.json, gzip'd and embedded by
 * scripts/embed-console-strings.mjs as build/webui_strings.c (payload/Makefile). The helper serves
 * them at GET /strings/<lang>.json with Content-Encoding: gzip and a year-long immutable cache: the
 * page asks with ?v=<hash of the strings>, so a new build is a new URL. */
#ifndef PS5UPLOAD_WEBUI_STRINGS_H
#define PS5UPLOAD_WEBUI_STRINGS_H

#include <stddef.h>

typedef struct {
    const char *lang;          /* "de", "zh-CN": [a-z]{2} or [a-z]{2}-[A-Z]{2} */
    const unsigned char *data; /* gzip of the language's {"cw_…":"…"} JSON */
    size_t len;
} webui_strings_entry_t;

/* The generated table (build/webui_strings.c). */
extern const webui_strings_entry_t webui_strings_table[];
extern const size_t webui_strings_count;

/* The entry a request path "/strings/<lang>.json" names, or NULL: <lang> must be two lowercase
 * letters, optionally "-" and two uppercase letters, and be in `tab`. Anything else ("../", other
 * case, other suffixes, an empty or longer id) is NULL. */
const webui_strings_entry_t *webui_strings_find(const webui_strings_entry_t *tab, size_t n, const char *path);

#endif
