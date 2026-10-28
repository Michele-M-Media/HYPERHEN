/* "Show on TV" / "Show on PC" handoffs and the file manager's management-method bodies: see
 * include/files_handoff.h. */
#include "files_handoff.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "webui_codec.h"

typedef struct {
    int set;
    long long at_ms;
    char path[FILES_PREFS_PATH_MAX];
} slot_t;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static slot_t g_slot[2]; /* ~2 KiB, static: never on a worker's stack */

int files_handoff_side(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "console") == 0) return FILES_HANDOFF_TO_CONSOLE;
    if (strcmp(name, "pc") == 0) return FILES_HANDOFF_TO_PC;
    return -1;
}

long long files_handoff_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int files_handoff_put(int side, const char *path, long long now_ms) {
    char clean[FILES_PREFS_PATH_MAX];
    if (side != FILES_HANDOFF_TO_CONSOLE && side != FILES_HANDOFF_TO_PC) return FILES_PREFS_E_BAD_OP;
    if (files_prefs_clean_path(path, clean) != 0) return FILES_PREFS_E_BAD_PATH;
    pthread_mutex_lock(&g_mu);
    memcpy(g_slot[side].path, clean, strlen(clean) + 1);
    g_slot[side].at_ms = now_ms;
    g_slot[side].set = 1;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int files_handoff_take(int side, long long now_ms, char *path, long long *age_ms) {
    int got = 0;
    if (side != FILES_HANDOFF_TO_CONSOLE && side != FILES_HANDOFF_TO_PC) return 0;
    pthread_mutex_lock(&g_mu);
    if (g_slot[side].set) {
        long long age = now_ms - g_slot[side].at_ms;
        if (age < 0) age = 0; /* a clock that stepped back: treat as fresh, it still expires */
        if (age < FILES_HANDOFF_TTL_MS) {
            memcpy(path, g_slot[side].path, strlen(g_slot[side].path) + 1);
            if (age_ms) *age_ms = age;
            got = 1;
        }
        g_slot[side].set = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return got;
}

void files_handoff_reset(void) {
    pthread_mutex_lock(&g_mu);
    memset(g_slot, 0, sizeof g_slot);
    pthread_mutex_unlock(&g_mu);
}

static int answer(char *out, size_t cap, const char *json) {
    size_t n = strlen(json);
    if (n + 1 > cap) return -1;
    memcpy(out, json, n + 1);
    return (int)n;
}

static int refuse(char *out, size_t cap, const char *token) {
    char buf[96];
    snprintf(buf, sizeof buf, "{\"ok\":false,\"err\":\"%s\"}", token);
    return answer(out, cap, buf);
}

int files_handoff_take_json(int side, long long now_ms, char *out, size_t cap) {
    char path[FILES_PREFS_PATH_MAX];
    long long age = 0;
    size_t at;
    /* before the take: a buffer too small must not empty the slot and lose the handoff */
    if (cap < FILES_HANDOFF_OUT_MAX) return -1;
    if (!files_handoff_take(side, now_ms, path, &age)) return answer(out, cap, "{}");
    at = (size_t)snprintf(out, cap, "{\"path\":\"");
    at += webui_json_escape(out + at, cap - at, path, strlen(path)); /* fits: 6x the path + 64 */
    snprintf(out + at, cap - at, "\",\"age_ms\":%lld}", age);
    return (int)strlen(out);
}

int files_uploads_json(const char *const *paths, int n, char *out, size_t cap) {
    static const char head[] = ",\"uploading\":[";
    char probe[1];
    size_t need = sizeof head - 1 + 2, at; /* head, "]}" */
    int i;
    for (i = 0; i < n; i++) need += webui_json_escape(probe, 0, paths[i], strlen(paths[i])) + 2 + (i ? 1 : 0);
    if (need + 1 > cap) return -1;
    memcpy(out, head, sizeof head - 1);
    at = sizeof head - 1;
    for (i = 0; i < n; i++) {
        if (i) out[at++] = ',';
        out[at++] = '"';
        at += webui_json_escape(out + at, cap - at, paths[i], strlen(paths[i]));
        out[at++] = '"';
    }
    memcpy(out + at, "]}", 3);
    return (int)(at + 2);
}

static const char *prefs_token(int rc) {
    switch (rc) {
    case FILES_PREFS_E_BAD_PATH: return "bad_path";
    case FILES_PREFS_E_BAD_OP: return "invalid_op";
    case FILES_PREFS_E_FULL: return "prefs_full";
    case FILES_PREFS_E_NOMEM: return "out_of_memory";
    default: return "save_failed";
    }
}

int files_mgmt_prefs_get(const char *file, char *out, size_t cap) {
    int len = files_prefs_get(file, out, cap);
    return len < 0 ? refuse(out, cap, prefs_token(len)) : len;
}

int files_mgmt_prefs_set(const char *file, const char *body, size_t n, char *out, size_t cap) {
    char op[16], path[FILES_PREFS_PATH_MAX];
    int o, len;
    if (n > FILES_PREFS_REQ_MAX) return refuse(out, cap, "too_large");
    if (webui_json_str(body, n, "op", op, sizeof op) != 1 || (o = files_prefs_op_from_name(op)) == 0)
        return refuse(out, cap, "invalid_op");
    if (webui_json_str(body, n, "path", path, sizeof path) != 1) return refuse(out, cap, "bad_path");
    len = files_prefs_update(file, o, path, out, cap);
    return len < 0 ? refuse(out, cap, prefs_token(len)) : len;
}

int files_mgmt_handoff_put(const char *body, size_t n, long long now_ms, char *out, size_t cap) {
    char side[16], path[FILES_PREFS_PATH_MAX];
    int s;
    if (n > FILES_HANDOFF_REQ_MAX) return refuse(out, cap, "too_large");
    if (webui_json_str(body, n, "to", side, sizeof side) != 1 || (s = files_handoff_side(side)) < 0)
        return refuse(out, cap, "invalid_side");
    if (webui_json_str(body, n, "path", path, sizeof path) != 1) return refuse(out, cap, "bad_path");
    if (files_handoff_put(s, path, now_ms) != 0) return refuse(out, cap, "bad_path");
    return answer(out, cap, "{\"ok\":true}");
}

int files_mgmt_handoff_take(const char *body, size_t n, long long now_ms, char *out, size_t cap) {
    char side[16];
    int s;
    if (n > FILES_HANDOFF_REQ_MAX) return refuse(out, cap, "too_large");
    if (webui_json_str(body, n, "for", side, sizeof side) != 1 || (s = files_handoff_side(side)) < 0)
        return refuse(out, cap, "invalid_side");
    return files_handoff_take_json(s, now_ms, out, cap);
}
