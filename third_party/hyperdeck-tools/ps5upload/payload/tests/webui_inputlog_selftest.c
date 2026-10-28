/* Host-side tests for the console page's input log (webui_inputlog.c): the {"events":[...]}
 * array becomes one line per event, only plain short ASCII strings are accepted, and the log
 * file is a ring that never grows past its cap, dropping its oldest whole lines. File tests run
 * in a fresh mkdtemp directory. Build: see the root Makefile test-payload. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "webui_inputlog.h"

static int failures = 0;

#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

static char g_dir[256];

static long lines_of(const char *arr, char *out, size_t cap) {
    return webui_inputlog_lines(arr, strlen(arr), out, cap);
}

static void lines_from_events(void) {
    char out[512];
    long n = lines_of("[\"1 keydown k=Enter kc=13\", \"2 click b=0 x=5 y=6\"]", out, sizeof out);
    CHECK(n == (long)strlen("1 keydown k=Enter kc=13\n2 click b=0 x=5 y=6\n"));
    CHECK(n > 0 && memcmp(out, "1 keydown k=Enter kc=13\n2 click b=0 x=5 y=6\n", (size_t)n) == 0);
    CHECK(lines_of("[]", out, sizeof out) == 0);
    CHECK(lines_of("[ ]", out, sizeof out) == 0);
}

static void lines_refuse_anything_but_plain_strings(void) {
    char out[512], big[WEBUI_INPUTLOG_LINE_MAX + 8];
    char many[8 * (WEBUI_INPUTLOG_EVENTS_MAX + 2)];
    size_t i, at = 0;
    CHECK(lines_of("[1]", out, sizeof out) == -1);
    CHECK(lines_of("[\"a\",{}]", out, sizeof out) == -1);
    CHECK(lines_of("[\"\"]", out, sizeof out) == -1);               /* empty */
    CHECK(lines_of("[\"a\\nb\"]", out, sizeof out) == -1);          /* an escape (a newline would forge a line) */
    CHECK(lines_of("[\"a\\\"b\"]", out, sizeof out) == -1);
    CHECK(lines_of("[\"caf\xc3\xa9\"]", out, sizeof out) == -1);    /* not ASCII */
    CHECK(lines_of("[\"a\tb\"]", out, sizeof out) == -1);           /* a control byte */
    CHECK(lines_of("[\"a\" \"b\"]", out, sizeof out) == -1);        /* no comma */
    CHECK(lines_of("[\"a\",]", out, sizeof out) == -1);
    CHECK(lines_of("[\"a\"", out, sizeof out) == -1);               /* cut short */
    /* one line too long */
    big[0] = '[';
    big[1] = '"';
    memset(big + 2, 'x', WEBUI_INPUTLOG_LINE_MAX + 1);
    memcpy(big + 2 + WEBUI_INPUTLOG_LINE_MAX + 1, "\"]", 3);
    CHECK(lines_of(big, out, sizeof out) == -1);
    /* too many events */
    many[at++] = '[';
    for (i = 0; i < WEBUI_INPUTLOG_EVENTS_MAX + 1; i++) {
        memcpy(many + at, i ? ",\"e\"" : "\"e\"", i ? 4 : 3);
        at += i ? 4 : 3;
    }
    many[at++] = ']';
    many[at] = '\0';
    {
        char *o = malloc(4 * WEBUI_INPUTLOG_EVENTS_MAX + 16);
        CHECK(o && webui_inputlog_lines(many, at, o, 4 * WEBUI_INPUTLOG_EVENTS_MAX + 16) == -1);
        free(o);
    }
    /* out too small */
    CHECK(lines_of("[\"abcdef\"]", out, 4) == -1);
}

static size_t file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (size_t)st.st_size : (size_t)-1;
}

static size_t read_all(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return 0;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static int count_entries(const char *dir) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL)
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d);
    return n;
}

static void ring_keeps_newest_whole_lines_under_cap(void) {
    char path[300], line[64], *work, *buf;
    const size_t cap = 1024, work_cap = 4096;
    int i;
    snprintf(path, sizeof path, "%s/webui-input.log", g_dir);
    work = malloc(work_cap);
    buf = malloc(work_cap);
    if (!work || !buf) exit(2);

    /* appends below the cap are plain appends */
    CHECK(webui_inputlog_append(path, "a\n", 2, cap, work, work_cap) == 0);
    CHECK(webui_inputlog_append(path, "b\n", 2, cap, work, work_cap) == 0);
    CHECK(file_size(path) == 4);
    CHECK(read_all(path, buf, work_cap) == 4 && memcmp(buf, "a\nb\n", 4) == 0);

    /* 200 numbered lines of 32 bytes: far past the 1 KiB cap */
    for (i = 0; i < 200; i++) {
        int k = snprintf(line, sizeof line, "%05d keydown k=ArrowDown kc=40\n", i);
        CHECK(k == 32);
        CHECK(webui_inputlog_append(path, line, (size_t)k, cap, work, work_cap) == 0);
        CHECK(file_size(path) <= cap);
    }
    {
        size_t n = read_all(path, buf, work_cap);
        CHECK(n > 0 && n <= cap);
        CHECK(n >= cap / 2); /* the ring keeps a useful amount, not just the last batch */
        CHECK(buf[n - 1] == '\n');
        CHECK(n >= 32 && memcmp(buf + n - 32, "00199 keydown", 13) == 0); /* the newest last */
        CHECK(n % 32 == 0 && buf[0] == '0');                            /* whole lines only */
    }
    /* no temp file is left in the directory */
    CHECK(count_entries(g_dir) == 1);

    /* one batch larger than the cap keeps only its own newest whole lines */
    {
        char *bigadd = malloc(4096);
        size_t at = 0;
        if (!bigadd) exit(2);
        for (i = 0; i < 64; i++) at += (size_t)snprintf(bigadd + at, 4096 - at, "%05d click b=0 x=1 y=2 d=1 e=-\n", 1000 + i);
        CHECK(webui_inputlog_append(path, bigadd, at, cap, work, work_cap) == 0);
        CHECK(file_size(path) <= cap);
        {
            size_t n = read_all(path, buf, work_cap);
            CHECK(n > 0 && buf[n - 1] == '\n' && memcmp(buf + n - 32, "01063 click", 11) == 0);
            CHECK(buf[0] == '0' && buf[5] == ' '); /* starts on a line */
        }
        free(bigadd);
    }

    /* a work buffer smaller than the file it must rewrite: refused, the file untouched */
    {
        size_t before = file_size(path);
        CHECK(webui_inputlog_append(path, buf, 900, cap, work, 64) == -1);
        CHECK(file_size(path) == before);
    }

    /* an unwritable place fails cleanly */
    CHECK(webui_inputlog_append("/nonexistent-dir/x.log", "a\n", 2, cap, work, work_cap) == -1);

    unlink(path);
    free(work);
    free(buf);
}

static void gate_admits_one_final_per_window(void) {
    webui_inputlog_gate g = WEBUI_INPUTLOG_GATE_INIT;
    const long long gap = 500;
    CHECK(webui_inputlog_gate_admit(&g, 1000, 0, gap) == 1);
    /* A regular batch too soon: refused, gate unchanged. */
    CHECK(webui_inputlog_gate_admit(&g, 1200, 0, gap) == 0);
    CHECK(g.last_ms == 1000);
    /* The final batch 300 ms after a regular one (Controls closed): admitted. */
    CHECK(webui_inputlog_gate_admit(&g, 1300, 1, gap) == 1);
    /* A second final, or a regular one, inside the window: refused (no flooding with the mark). */
    CHECK(webui_inputlog_gate_admit(&g, 1400, 1, gap) == 0);
    CHECK(webui_inputlog_gate_admit(&g, 1700, 0, gap) == 0);
    CHECK(webui_inputlog_gate_admit(&g, 1790, 1, gap) == 0);
    /* A full gap after the final: regular traffic again. */
    CHECK(webui_inputlog_gate_admit(&g, 1800, 0, gap) == 1);
    /* Final right after that: the last final was 500+ ms ago, so admitted once more. */
    CHECK(webui_inputlog_gate_admit(&g, 1850, 1, gap) == 1);
    CHECK(webui_inputlog_gate_admit(&g, 1900, 1, gap) == 0);
    /* A final that is on time anyway does not use the exemption. */
    webui_inputlog_gate h = WEBUI_INPUTLOG_GATE_INIT;
    CHECK(webui_inputlog_gate_admit(&h, 5000, 1, gap) == 1);
    CHECK(h.last_final_ms == -1000000);
    CHECK(webui_inputlog_gate_admit(&h, 5100, 1, gap) == 1);
    CHECK(webui_inputlog_gate_admit(&h, 5200, 1, gap) == 0);
}

int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/ps5upload-inputlog-XXXXXX");
    if (!mkdtemp(g_dir)) {
        perror("mkdtemp");
        return 2;
    }
    lines_from_events();
    lines_refuse_anything_but_plain_strings();
    ring_keeps_newest_whole_lines_under_cap();
    gate_admits_one_final_per_window();
    rmdir(g_dir);
    if (failures) {
        fprintf(stderr, "webui_inputlog_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("webui_inputlog_selftest: all passed\n");
    return 0;
}
