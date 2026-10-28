/* Host-side tests for the console web UI's settings file (webui_settings.c) and the pure parts of
 * the home-screen tile (webui_tile.c built with WEBUI_TILE_HOST_TEST: the Sony register/unregister
 * half is left out): defaults for a missing or garbled file, save/load round trip through a
 * same-directory temp file, the tile's param.json byte for byte, and the tile folder being
 * written only when its bytes differ. File tests run in a fresh mkdtemp directory.
 * Build: see the root Makefile test-payload. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "webui_settings.h"
#include "webui_tile.h"

static int failures = 0;

#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

/* The embedded icon is generated into payload/build/ at ELF build time; the test brings its own. */
unsigned char webui_tile_icon0_png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 1, 2, 3 };
unsigned int webui_tile_icon0_png_len = sizeof webui_tile_icon0_png;

static char g_dir[256];

static void put_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static size_t get_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return (size_t)-1;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
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

static void missing_file_gives_defaults(void) {
    char p[512];
    webui_settings_t s = { 7, 7 };
    snprintf(p, sizeof p, "%s/none.json", g_dir);
    CHECK(webui_settings_load(p, &s) == 1);
    CHECK(s.lan == 0 && s.tile == 1);
}

static void garbled_file_gives_defaults(void) {
    static const char *bad[] = {
        "",
        "garbage",
        "{\"lan\":true",             /* cut short */
        "{\"lan\":1,\"tile\":true}", /* not a bool */
        "{\"lan\":true,\"tile\":\"no\"}",
        "[true,false]",
        "\x00\x01\x02",
    };
    char p[512];
    size_t i;
    snprintf(p, sizeof p, "%s/bad.json", g_dir);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        webui_settings_t s = { 7, 7 };
        put_file(p, bad[i]);
        CHECK(webui_settings_load(p, &s) == 1);
        CHECK(s.lan == 0 && s.tile == 1);
    }
    /* A valid object missing a key keeps that key's default. */
    {
        webui_settings_t s = { 7, 7 };
        put_file(p, "{\"lan\":true}");
        CHECK(webui_settings_load(p, &s) == 0);
        CHECK(s.lan == 1 && s.tile == 1);
        put_file(p, " {\"tile\":false}\n");
        CHECK(webui_settings_load(p, &s) == 0);
        CHECK(s.lan == 0 && s.tile == 0);
    }
    unlink(p);
}

static void save_then_load_round_trips(void) {
    char p[512], text[256], sub[512];
    int lan, tile;
    snprintf(p, sizeof p, "%s/webui.json", g_dir);
    for (lan = 0; lan < 2; lan++)
        for (tile = 0; tile < 2; tile++) {
            webui_settings_t in = { lan, tile }, out = { 7, 7 };
            CHECK(webui_settings_save(p, &in) == 0);
            CHECK(webui_settings_load(p, &out) == 0);
            CHECK(out.lan == lan && out.tile == tile);
        }
    /* The exact file text, and nothing left behind but the file itself. */
    {
        webui_settings_t d = { 0, 1 };
        CHECK(webui_settings_save(p, &d) == 0);
        CHECK(get_file(p, text, sizeof text) != (size_t)-1);
        CHECK(strcmp(text, "{\"lan\":false,\"tile\":true}") == 0);
        CHECK(count_entries(g_dir) == 1);
    }
    /* A save that cannot happen leaves no file and says so. */
    {
        webui_settings_t d = { 1, 0 };
        snprintf(sub, sizeof sub, "%s/no-such-dir/webui.json", g_dir);
        CHECK(webui_settings_save(sub, &d) == -1);
    }
    unlink(p);
}

static void param_json_exact(void) {
    static const char want[] =
        "{\"applicationCategoryType\":66048,\"titleId\":\"PSUP00001\","
        "\"deeplinkUri\":\"http://127.0.0.1:9180/\","
        "\"localizedParameters\":{\"defaultLanguage\":\"en-US\",\"en-US\":{\"titleName\":\"PS5Upload Minion\"}}}";
    char buf[512];
    size_t n = webui_tile_param_json(buf, sizeof buf, 9180);
    CHECK(n == sizeof want - 1);
    CHECK(strcmp(buf, want) == 0);
    /* Too small: the full length is still reported, and nothing is overrun. */
    {
        char tiny[16];
        memset(tiny, 'x', sizeof tiny);
        CHECK(webui_tile_param_json(tiny, sizeof tiny, 9180) == sizeof want - 1);
        CHECK(tiny[sizeof tiny - 1] == '\0');
    }
}

static void tile_files_written_only_when_different(void) {
    char dir[512], p[600], buf[1024], want[512];
    struct stat st;
    snprintf(dir, sizeof dir, "%s/tile/PSUP00001", g_dir);
    CHECK(webui_tile_write_files(dir, 9180) == 3); /* param.json, icon0.png, eboot.bin */
    snprintf(p, sizeof p, "%s/sce_sys/param.json", dir);
    webui_tile_param_json(want, sizeof want, 9180);
    CHECK(get_file(p, buf, sizeof buf) == strlen(want));
    CHECK(strcmp(buf, want) == 0);
    snprintf(p, sizeof p, "%s/sce_sys/icon0.png", dir);
    CHECK(stat(p, &st) == 0 && (size_t)st.st_size == sizeof webui_tile_icon0_png);
    snprintf(p, sizeof p, "%s/eboot.bin", dir);
    CHECK(stat(p, &st) == 0 && st.st_size > 0);
    /* Unchanged: nothing is rewritten. */
    CHECK(webui_tile_write_files(dir, 9180) == 0);
    /* One file differs: only that one is rewritten; another port changes param.json. */
    snprintf(p, sizeof p, "%s/sce_sys/icon0.png", dir);
    put_file(p, "not the icon");
    CHECK(webui_tile_write_files(dir, 9180) == 1);
    CHECK(stat(p, &st) == 0 && (size_t)st.st_size == sizeof webui_tile_icon0_png);
    CHECK(webui_tile_write_files(dir, 9181) == 1);
    CHECK(webui_tile_write_files(dir, 9181) == 0);
    /* No temp files left beside them. */
    CHECK(count_entries(dir) == 2); /* sce_sys, eboot.bin */
    snprintf(p, sizeof p, "%s/sce_sys", dir);
    CHECK(count_entries(p) == 2);   /* param.json, icon0.png */
    /* Removing the folder takes everything with it. */
    CHECK(webui_tile_remove_files(dir) == 0);
    CHECK(stat(dir, &st) != 0);
    CHECK(webui_tile_remove_files(dir) == 0); /* already gone is fine */
    snprintf(p, sizeof p, "%s/tile", g_dir);
    rmdir(p);
}

/* ensure's decision: every combination of (files changed, mount live, mount.lnk ours, appmeta). */
static void tile_plan_decides_rebind_or_register(void) {
    int c, m, l, a;
    for (c = 0; c < 2; c++)
        for (m = 0; m < 2; m++)
            for (l = 0; l < 2; l++)
                for (a = 0; a < 2; a++) {
                    webui_tile_action_t want;
                    if (c) want = WEBUI_TILE_REGISTER;           /* the copies must be refreshed */
                    else if (m && l) want = WEBUI_TILE_NONE;     /* registered this boot */
                    else if (!m && l && a) want = WEBUI_TILE_REBIND; /* after a reboot: re-mount only */
                    else want = WEBUI_TILE_REGISTER;             /* never ours, half-removed, or someone else's bind */
                    if (webui_tile_plan(c, m, l, a) != want) {
                        fprintf(stderr, "FAIL plan(changed=%d mount=%d lnk=%d appmeta=%d) = %d, want %d\n", c, m, l, a,
                                (int)webui_tile_plan(c, m, l, a), (int)want);
                        failures++;
                    }
                }
    /* The cases the review named, spelled out. */
    CHECK(webui_tile_plan(0, 0, 1, 1) == WEBUI_TILE_REBIND);
    CHECK(webui_tile_plan(0, 0, 1, 0) == WEBUI_TILE_REGISTER);
    CHECK(webui_tile_plan(0, 0, 0, 1) == WEBUI_TILE_REGISTER);
    CHECK(webui_tile_plan(0, 1, 1, 1) == WEBUI_TILE_NONE);
    CHECK(webui_tile_plan(1, 1, 1, 1) == WEBUI_TILE_REGISTER);
}

int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/ps5upload-webui-settings-XXXXXX");
    if (!mkdtemp(g_dir)) {
        perror("mkdtemp");
        return 2;
    }
    missing_file_gives_defaults();
    garbled_file_gives_defaults();
    save_then_load_round_trips();
    param_json_exact();
    tile_files_written_only_when_different();
    tile_plan_decides_rebind_or_register();
    rmdir(g_dir);
    if (failures) {
        fprintf(stderr, "webui settings self-test: %d failure(s)\n", failures);
        return 1;
    }
    printf("webui settings self-test: ok\n");
    return 0;
}
