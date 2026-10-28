/* Host test for the file index's name match (include/index_glob.h): the glob semantics INDEX_SEARCH
 * always had (* any run, ? one byte, ASCII case-insensitive, whole name), and a bound on the
 * pathological patterns that made the old recursive matcher exponential. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "index_glob.h"

static int failures = 0;
#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

int main(void) {
    char name[256];
    struct timespec a, b;
    double ms;
    int i, hits = 0;
    CHECK(index_glob_match("*", ""));
    CHECK(index_glob_match("*", "eboot.bin"));
    CHECK(index_glob_match("", ""));
    CHECK(!index_glob_match("", "a"));
    CHECK(index_glob_match("*eboot*", "eboot.bin"));
    CHECK(index_glob_match("*EBOOT*", "my_eboot.BIN"));
    CHECK(index_glob_match("*.pkg", "Game.PKG"));
    CHECK(!index_glob_match("*.pkg", "Game.pkg.part"));
    CHECK(index_glob_match("CUSA0784?", "cusa07842"));
    CHECK(!index_glob_match("CUSA0784?", "CUSA0784"));
    CHECK(!index_glob_match("CUSA0784?", "CUSA078422"));
    CHECK(index_glob_match("a*b*c", "aXXbYYc"));
    CHECK(index_glob_match("a*b*c", "abc"));
    CHECK(!index_glob_match("a*b*c", "aXXbYY"));
    CHECK(index_glob_match("a**b", "ab"));
    CHECK(index_glob_match("*a*", "banana"));
    CHECK(index_glob_match("*ana", "banana"));
    CHECK(!index_glob_match("*anaz", "banana"));
    CHECK(index_glob_match("?*", "x"));
    CHECK(!index_glob_match("?*", ""));
    CHECK(index_glob_match("*?", "x"));
    CHECK(index_glob_match("\xc3\xa4*", "\xc3\xa4pfel")); /* UTF-8 bytes compare as themselves */
    CHECK(!index_glob_match("\xc3\x84*", "\xc3\xa4pfel")); /* non-ASCII case is not folded */
    /* the pathological pattern: 200,000 names in well under a second (the recursive matcher took
     * ~0.4 s for ONE of them) */
    memset(name, 'a', 200);
    name[200] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < 200000; i++) hits += index_glob_match("*?*?*?*?*?*?*?*z", name);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ms = (double)(b.tv_sec - a.tv_sec) * 1e3 + (double)(b.tv_nsec - a.tv_nsec) / 1e6;
    CHECK(hits == 0);
    CHECK(ms < 5000.0);
    name[199] = 'z';
    CHECK(index_glob_match("*?*?*?*?*?*?*?*z", name));
    if (failures) {
        fprintf(stderr, "index_glob_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("index_glob_selftest: all passed (pathological pattern x200000: %.0f ms)\n", ms);
    return 0;
}
