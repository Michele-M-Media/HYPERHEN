/* GET /strings/<lang>.json's lookup: see include/webui_strings.h. */
#include "webui_strings.h"

#include <string.h>

#define STRINGS_PREFIX "/strings/"
#define STRINGS_SUFFIX ".json"

static int lower(char c) { return c >= 'a' && c <= 'z'; }
static int upper(char c) { return c >= 'A' && c <= 'Z'; }

const webui_strings_entry_t *webui_strings_find(const webui_strings_entry_t *tab, size_t n, const char *path) {
    const char *id;
    size_t len, i;
    if (!tab || !path || strncmp(path, STRINGS_PREFIX, sizeof STRINGS_PREFIX - 1) != 0) return NULL;
    id = path + sizeof STRINGS_PREFIX - 1;
    len = strlen(id);
    if (len < sizeof STRINGS_SUFFIX - 1 || strcmp(id + len - (sizeof STRINGS_SUFFIX - 1), STRINGS_SUFFIX) != 0) return NULL;
    len -= sizeof STRINGS_SUFFIX - 1;
    /* ll or ll-RR, nothing else */
    if (len != 2 && len != 5) return NULL;
    if (!lower(id[0]) || !lower(id[1])) return NULL;
    if (len == 5 && (id[2] != '-' || !upper(id[3]) || !upper(id[4]))) return NULL;
    for (i = 0; i < n; i++) {
        if (tab[i].lang && strlen(tab[i].lang) == len && memcmp(tab[i].lang, id, len) == 0) return &tab[i];
    }
    return NULL;
}
