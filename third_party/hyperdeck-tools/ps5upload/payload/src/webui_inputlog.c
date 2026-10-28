/* The console page's input log: see include/webui_inputlog.h. */
#include "webui_inputlog.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *skip_ws(const char *p, const char *e) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

long webui_inputlog_lines(const char *arr, size_t n, char *out, size_t cap) {
    const char *p = arr, *e = arr + n;
    size_t at = 0, count = 0;
    p = skip_ws(p, e);
    if (p >= e || *p != '[') return -1;
    p = skip_ws(p + 1, e);
    if (p < e && *p == ']') return skip_ws(p + 1, e) == e ? 0 : -1;
    for (;;) {
        const char *s;
        size_t len;
        if (p >= e || *p != '"') return -1;
        s = ++p;
        while (p < e && *p != '"') {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c > 0x7e || c == '\\') return -1;
            p++;
        }
        if (p >= e) return -1;
        len = (size_t)(p - s);
        p++;
        if (len == 0 || len > WEBUI_INPUTLOG_LINE_MAX || ++count > WEBUI_INPUTLOG_EVENTS_MAX) return -1;
        if (at > cap || len + 1 > cap - at) return -1;
        memcpy(out + at, s, len);
        out[at + len] = '\n';
        at += len + 1;
        p = skip_ws(p, e);
        if (p < e && *p == ',') {
            p = skip_ws(p + 1, e);
            continue;
        }
        if (p < e && *p == ']') break;
        return -1;
    }
    return skip_ws(p + 1, e) == e ? (long)at : -1;
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, buf + done, len - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        done += (size_t)w;
    }
    return 0;
}

/* Reads exactly len bytes at off: 0, or -1. */
static int read_at(int fd, char *buf, size_t len, off_t off) {
    size_t done = 0;
    while (done < len) {
        ssize_t r = pread(fd, buf + done, len - done, off + (off_t)done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

/* The index just past the first '\n' in buf[0..len), or len when there is none. */
static size_t after_first_newline(const char *buf, size_t len) {
    const char *nl = memchr(buf, '\n', len);
    return nl ? (size_t)(nl - buf) + 1 : len;
}

int webui_inputlog_append(const char *path, const char *add, size_t add_len, size_t cap, char *work, size_t work_cap) {
    struct stat st;
    size_t old = 0, target, len = 0;
    char tmp[1024];
    int fd, k;

    if (add_len == 0) return 0;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) old = (size_t)st.st_size;
    if (old + add_len <= cap) {
        fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd < 0) return -1;
        k = write_all(fd, add, add_len);
        if (close(fd) != 0) k = -1;
        return k;
    }

    /* Rewrite: the newest whole lines of the file, then `add`, within target. */
    target = cap - cap / 4;
    if (work_cap < cap || target == 0) return -1;
    if (add_len >= target) {
        /* add alone fills it: its newest whole lines (cut after the first newline past the excess) */
        size_t skip = add_len - target, start = skip ? skip + after_first_newline(add + skip, add_len - skip) : 0;
        if (skip && add[skip - 1] == '\n') start = skip; /* the cut already sits on a line start */
        len = add_len - start;
        memcpy(work, add + start, len);
    } else {
        size_t keep = target - add_len, start;
        /* one byte more than keep, from before the cut: it tells whether the cut is on a line start */
        if (old > keep) {
            fd = open(path, O_RDONLY);
            if (fd < 0) return -1;
            if (read_at(fd, work, keep + 1, (off_t)(old - keep - 1)) != 0) {
                close(fd);
                return -1;
            }
            close(fd);
            start = after_first_newline(work, keep + 1);
            len = keep + 1 - start;
            memmove(work, work + start, len);
        }
        memcpy(work + len, add, add_len);
        len += add_len;
    }

    k = snprintf(tmp, sizeof tmp, "%s.tmp", path); /* same directory as `path`: the rename never crosses a mount */
    if (k < 0 || (size_t)k >= sizeof tmp) return -1;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    if (write_all(fd, work, len) != 0 || fsync(fd) != 0) {
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

int webui_inputlog_gate_admit(webui_inputlog_gate *g, long long now_ms, int final, long long gap_ms) {
    if (now_ms - g->last_ms >= gap_ms) {
        g->last_ms = now_ms;
        return 1;
    }
    if (final && now_ms - g->last_final_ms >= gap_ms) {
        g->last_ms = now_ms;
        g->last_final_ms = now_ms;
        return 1;
    }
    return 0;
}
