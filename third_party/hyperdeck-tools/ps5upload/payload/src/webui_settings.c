/* The console web UI's settings file: see include/webui_settings.h. */
#include "webui_settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "webui_codec.h"

#define SETTINGS_MAX 4096 /* the file is ~30 bytes; anything this large is not ours */

void webui_settings_defaults(webui_settings_t *out) {
    out->lan = 0;
#ifdef HYPERHEN_NO_MEDIA_TILE
    out->tile = 0;
#else
    out->tile = 1;
#endif
}

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int webui_settings_load(const char *path, webui_settings_t *out) {
    char buf[SETTINGS_MAX + 1];
    size_t n = 0, a, b;
    webui_settings_t s;
    int fd, v, rc;

    webui_settings_defaults(out);
    fd = open(path, O_RDONLY);
    if (fd < 0) return 1;
    for (;;) {
        ssize_t r = read(fd, buf + n, sizeof buf - 1 - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
        if (n == sizeof buf - 1) break;
    }
    close(fd);
    if (n >= SETTINGS_MAX) return 1;
    buf[n] = '\0';
    /* One JSON object: the first and last non-blank bytes are its braces (a cut-short file is
     * garbled, even when the keys it still has would read). */
    for (a = 0; a < n && is_ws(buf[a]); a++) {}
    for (b = n; b > a && is_ws(buf[b - 1]); b--) {}
    if (b - a < 2 || buf[a] != '{' || buf[b - 1] != '}') return 1;

    webui_settings_defaults(&s);
    rc = webui_json_bool(buf + a, b - a, "lan", &v);
    if (rc < 0) return 1;
    if (rc == 1) s.lan = v;
    rc = webui_json_bool(buf + a, b - a, "tile", &v);
    if (rc < 0) return 1;
    if (rc == 1) s.tile = v;
#ifdef HYPERHEN_NO_MEDIA_TILE
    s.tile = 0;
#endif
    *out = s;
    return 0;
}

int webui_settings_save(const char *path, const webui_settings_t *s) {
    char tmp[1024], text[64];
    size_t len, done = 0;
    int fd, k;

    k = snprintf(tmp, sizeof tmp, "%s.tmp", path); /* same directory as `path`: the rename never crosses a mount */
    if (k < 0 || (size_t)k >= sizeof tmp) return -1;
    k = snprintf(text, sizeof text, "{\"lan\":%s,\"tile\":%s}", s->lan ? "true" : "false", s->tile ? "true" : "false");
    len = (size_t)k;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    while (done < len) {
        ssize_t w = write(fd, text + done, len - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += (size_t)w;
    }
    if (done != len || fsync(fd) != 0) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    if (close(fd) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}
