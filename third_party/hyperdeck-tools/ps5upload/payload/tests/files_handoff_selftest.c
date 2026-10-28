/* Host-side tests for the Show on TV / Show on PC handoffs and the file manager's management-method
 * bodies (files_handoff.c): one slot per direction, a take empties it, two minutes and it is gone,
 * paths cleaned and bounded, the JSON both ways, the prefs reached through files.prefs_get/_set (in a
 * fresh mkdtemp directory), the /api/jobs "uploading" list, and concurrent puts and takes.
 * Build: see the root Makefile test-payload. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "files_handoff.h"

static int failures = 0;

#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

#define B(s) s, strlen(s)

static char g_out[FILES_PREFS_FILE_MAX + 1];

static void test_slots(void) {
    char path[FILES_PREFS_PATH_MAX];
    long long age = -1;
    files_handoff_reset();
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, 0, path, &age) == 0);
    CHECK(files_handoff_put(FILES_HANDOFF_TO_CONSOLE, "/data//games/", 1000) == 0);
    /* the other side's slot is untouched */
    CHECK(files_handoff_take(FILES_HANDOFF_TO_PC, 1000, path, &age) == 0);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, 1500, path, &age) == 1);
    CHECK(strcmp(path, "/data/games") == 0 && age == 500);
    /* a take empties it */
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, 1600, path, &age) == 0);
    /* a put replaces */
    CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, "/mnt/usb0", 10) == 0);
    CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, "/mnt/ext0", 20) == 0);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_PC, 30, path, &age) == 1 && strcmp(path, "/mnt/ext0") == 0 && age == 10);
    /* expiry: 1 ms short of two minutes still opens, two minutes is gone (and cleared) */
    CHECK(files_handoff_put(FILES_HANDOFF_TO_CONSOLE, "/data", 0) == 0);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, FILES_HANDOFF_TTL_MS - 1, path, &age) == 1);
    CHECK(files_handoff_put(FILES_HANDOFF_TO_CONSOLE, "/data", 0) == 0);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, FILES_HANDOFF_TTL_MS, path, &age) == 0);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, 0, path, &age) == 0); /* not kept after expiring */
    /* a clock that stepped back reads as fresh */
    CHECK(files_handoff_put(FILES_HANDOFF_TO_CONSOLE, "/data", 5000) == 0);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_CONSOLE, 4000, path, &age) == 1 && age == 0);
    /* refusals leave the slot as it was */
    CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, "/data/ok", 0) == 0);
    CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, "relative", 0) == FILES_PREFS_E_BAD_PATH);
    CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, "/data/../etc", 0) == FILES_PREFS_E_BAD_PATH);
    CHECK(files_handoff_put(7, "/data", 0) == FILES_PREFS_E_BAD_OP);
    CHECK(files_handoff_take(FILES_HANDOFF_TO_PC, 1, path, &age) == 1 && strcmp(path, "/data/ok") == 0);
    CHECK(files_handoff_take(-1, 1, path, &age) == 0);
    CHECK(files_handoff_side("console") == FILES_HANDOFF_TO_CONSOLE && files_handoff_side("pc") == FILES_HANDOFF_TO_PC &&
          files_handoff_side("tv") == -1 && files_handoff_side("") == -1 && files_handoff_side(NULL) == -1);
    {
        /* the longest path that fits, and one byte more */
        char big[FILES_PREFS_PATH_MAX + 2];
        memset(big, 'a', sizeof big);
        big[0] = '/';
        big[FILES_PREFS_PATH_MAX - 1] = '\0';
        CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, big, 0) == 0);
        CHECK(files_handoff_take(FILES_HANDOFF_TO_PC, 0, path, &age) == 1 && strlen(path) == FILES_PREFS_PATH_MAX - 1);
        big[FILES_PREFS_PATH_MAX - 1] = 'a';
        big[FILES_PREFS_PATH_MAX] = '\0';
        CHECK(files_handoff_put(FILES_HANDOFF_TO_PC, big, 0) == FILES_PREFS_E_BAD_PATH);
    }
}

static void test_json(void) {
    char out[FILES_HANDOFF_OUT_MAX];
    int n;
    files_handoff_reset();
    n = files_handoff_take_json(FILES_HANDOFF_TO_CONSOLE, 0, out, sizeof out);
    CHECK(n == 2 && strcmp(out, "{}") == 0);
    CHECK(files_handoff_put(FILES_HANDOFF_TO_CONSOLE, "/data/My \"Game\" \\ 游戏", 100) == 0);
    n = files_handoff_take_json(FILES_HANDOFF_TO_CONSOLE, 350, out, sizeof out);
    CHECK(n > 0 && strcmp(out, "{\"path\":\"/data/My \\\"Game\\\" \\\\ 游戏\",\"age_ms\":250}") == 0);
    /* a buffer too small refuses before taking: the handoff is still there */
    CHECK(files_handoff_put(FILES_HANDOFF_TO_CONSOLE, "/data/keep", 0) == 0);
    CHECK(files_handoff_take_json(FILES_HANDOFF_TO_CONSOLE, 1, out, 16) == -1);
    n = files_handoff_take_json(FILES_HANDOFF_TO_CONSOLE, 1, out, sizeof out);
    CHECK(n > 0 && strstr(out, "/data/keep") != NULL);
    /* through the method bodies */
    n = files_mgmt_handoff_put(B("{\"to\":\"console\",\"path\":\"/mnt/usb0/x\"}"), 0, out, sizeof out);
    CHECK(n > 0 && strcmp(out, "{\"ok\":true}") == 0);
    n = files_mgmt_handoff_take(B("{\"for\":\"pc\"}"), 1, out, sizeof out);
    CHECK(n > 0 && strcmp(out, "{}") == 0);
    n = files_mgmt_handoff_take(B("{\"for\":\"console\"}"), 1, out, sizeof out);
    CHECK(n > 0 && strcmp(out, "{\"path\":\"/mnt/usb0/x\",\"age_ms\":1}") == 0);
    n = files_mgmt_handoff_put(B("{\"to\":\"pc\",\"path\":\"/data\"}"), 0, out, sizeof out);
    CHECK(n > 0 && strcmp(out, "{\"ok\":true}") == 0);
    n = files_mgmt_handoff_take(B("{\"for\":\"pc\"}"), FILES_HANDOFF_TTL_MS + 1, out, sizeof out);
    CHECK(n > 0 && strcmp(out, "{}") == 0); /* expired */
    /* refusals: the dispatcher turns {"ok":false,"err":..} into an error status */
    n = files_mgmt_handoff_put(B("{\"to\":\"tv\",\"path\":\"/data\"}"), 0, out, sizeof out);
    CHECK(n > 0 && strstr(out, "\"ok\":false") && strstr(out, "invalid_side"));
    n = files_mgmt_handoff_put(B("{\"to\":\"pc\",\"path\":\"data\"}"), 0, out, sizeof out);
    CHECK(n > 0 && strstr(out, "\"ok\":false") && strstr(out, "bad_path"));
    n = files_mgmt_handoff_put(B("{\"to\":\"pc\"}"), 0, out, sizeof out);
    CHECK(n > 0 && strstr(out, "bad_path"));
    n = files_mgmt_handoff_take(B("{}"), 0, out, sizeof out);
    CHECK(n > 0 && strstr(out, "invalid_side"));
    n = files_mgmt_handoff_take("{\"for\":\"pc\"}", FILES_HANDOFF_REQ_MAX + 1, 0, out, sizeof out);
    CHECK(n > 0 && strstr(out, "too_large"));
}

static void test_prefs_via_methods(void) {
    char dir[] = "/tmp/ps5u-handoff-XXXXXX", file[64];
    const char *pin = "{\"op\":\"pin\",\"path\":\"/data/homebrew/\"}";
    int n;
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        exit(2);
    }
    snprintf(file, sizeof file, "%s/files-prefs.json", dir);
    n = files_mgmt_prefs_get(file, g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"favorites\":[]") != NULL);
    n = files_mgmt_prefs_set(file, pin, strlen(pin), g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"favorites\":[\"/data/homebrew\"]") != NULL);
    /* what the console page's route reads is what the method wrote */
    n = files_mgmt_prefs_get(file, g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"favorites\":[\"/data/homebrew\"]") != NULL);
    n = files_mgmt_prefs_set(file, B("{\"op\":\"unpin\",\"path\":\"/data/homebrew\"}"), g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"favorites\":[]") != NULL);
    n = files_mgmt_prefs_set(file, B("{\"op\":\"visit\",\"path\":\"/mnt/usb0\"}"), g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"last\":\"/mnt/usb0\"") != NULL);
    n = files_mgmt_prefs_set(file, B("{\"op\":\"drop\",\"path\":\"/data\"}"), g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"ok\":false") && strstr(g_out, "invalid_op"));
    n = files_mgmt_prefs_set(file, B("{\"op\":\"pin\",\"path\":\"/a/../b\"}"), g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "bad_path"));
    n = files_mgmt_prefs_set(file, B("{\"op\":\"pin\"}"), g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "bad_path"));
    n = files_mgmt_prefs_set(file, pin, FILES_PREFS_REQ_MAX + 1, g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "too_large"));
    /* the file still parses after the refusals */
    n = files_mgmt_prefs_get(file, g_out, sizeof g_out);
    CHECK(n > 0 && strstr(g_out, "\"last\":\"/mnt/usb0\"") != NULL);
    unlink(file);
    rmdir(dir);
}

static void test_uploads_json(void) {
    char out[256];
    const char *paths[] = { "/data/homebrew/Game", "/mnt/ext0/a\"b" };
    int n;
    n = files_uploads_json(paths, 0, out, sizeof out);
    CHECK(n > 0 && strcmp(out, ",\"uploading\":[]}") == 0);
    n = files_uploads_json(paths, 2, out, sizeof out);
    CHECK(n == (int)strlen(out) && strcmp(out, ",\"uploading\":[\"/data/homebrew/Game\",\"/mnt/ext0/a\\\"b\"]}") == 0);
    /* too small: nothing written */
    memcpy(out, "}", 2);
    CHECK(files_uploads_json(paths, 2, out, 20) == -1 && strcmp(out, "}") == 0);
}

#define THREADS 8
static void *hammer(void *arg) {
    char path[FILES_PREFS_PATH_MAX], p[32];
    long long age;
    int i, id = (int)(long)arg;
    for (i = 0; i < 2000; i++) {
        snprintf(p, sizeof p, "/data/t%d/%d", id, i);
        if (files_handoff_put(id & 1, p, i) != 0) failures++;
        if (files_handoff_take((id + 1) & 1, i, path, &age) == 1 && strncmp(path, "/data/t", 7) != 0) failures++;
    }
    return NULL;
}

static void test_concurrent(void) {
    pthread_t t[THREADS];
    long i;
    files_handoff_reset();
    for (i = 0; i < THREADS; i++) pthread_create(&t[i], NULL, hammer, (void *)i);
    for (i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
}

int main(void) {
    test_slots();
    test_json();
    test_prefs_via_methods();
    test_uploads_json();
    test_concurrent();
    if (failures) {
        fprintf(stderr, "files_handoff_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("files_handoff_selftest: ok\n");
    return 0;
}
