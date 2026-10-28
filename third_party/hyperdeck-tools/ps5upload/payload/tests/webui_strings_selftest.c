/* Host-side tests for GET /strings/<lang>.json's lookup (webui_strings.c): only ll / ll-RR ids
 * that are in the table resolve; traversal, other case, other suffixes and unknown ids do not.
 * Build: see the root Makefile test-payload. */
#include <stdio.h>
#include <string.h>

#include "webui_strings.h"

static int failures = 0;

#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

static const unsigned char k_de[] = { 0x1f, 0x8b, 1 };
static const unsigned char k_zh[] = { 0x1f, 0x8b, 2 };
static const unsigned char k_pt[] = { 0x1f, 0x8b, 3 };
static const webui_strings_entry_t k_tab[] = {
    { "de", k_de, sizeof k_de },
    { "zh-CN", k_zh, sizeof k_zh },
    { "pt-BR", k_pt, sizeof k_pt },
};
#define N (sizeof k_tab / sizeof k_tab[0])

static const webui_strings_entry_t *find(const char *p) { return webui_strings_find(k_tab, N, p); }

static void known_languages_resolve(void) {
    CHECK(find("/strings/de.json") == &k_tab[0]);
    CHECK(find("/strings/zh-CN.json") == &k_tab[1]);
    CHECK(find("/strings/pt-BR.json") == &k_tab[2]);
    CHECK(find("/strings/de.json")->len == 3);
}

static void unknown_or_malformed_ids_do_not(void) {
    static const char *bad[] = {
        "/strings/fr.json",          /* well-formed, not in the table */
        "/strings/en.json",          /* English lives in the page */
        "/strings/DE.json", "/strings/zh-cn.json", "/strings/ZH-CN.json", "/strings/zh_CN.json",
        "/strings/../de.json", "/strings/..%2fde.json", "/strings/de/../de.json", "/strings//de.json",
        "/strings/de.json/", "/strings/de.jsonx", "/strings/de.JSON", "/strings/de", "/strings/.json",
        "/strings/", "/strings", "/strings/d.json", "/strings/deu.json", "/strings/zh-CNN.json",
        "/strings/zh-C.json", "/strings/de.json.gz", "/strings/de\x01.json", "strings/de.json",
        "/Strings/de.json", "/api/strings/de.json", "//strings/de.json", "/strings/zh-CN.json\x80", "",
    };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (find(bad[i]) != NULL) {
            fprintf(stderr, "FAIL: \"%s\" resolved\n", bad[i]);
            failures++;
        }
    }
    CHECK(webui_strings_find(k_tab, N, NULL) == NULL);
    CHECK(webui_strings_find(NULL, 0, "/strings/de.json") == NULL);
    CHECK(webui_strings_find(k_tab, 0, "/strings/de.json") == NULL);
}

int main(void) {
    known_languages_resolve();
    unknown_or_malformed_ids_do_not();
    if (failures) {
        fprintf(stderr, "webui_strings_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("webui_strings_selftest: ok\n");
    return 0;
}
