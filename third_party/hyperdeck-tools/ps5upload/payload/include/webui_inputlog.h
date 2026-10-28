/* The console page's input log: what the PS5 browser delivers for each controller button,
 * measured on the console. POST /api/input-log {"events":["<line>",...]} (webui_api.c) appends
 * one line per event to WEBUI_INPUTLOG_PATH, a ring that never grows past WEBUI_INPUTLOG_CAP.
 * The page (client/src/console/inputLog.ts) sends only while its Controls screen asks for it, at
 * most one request a second, each body at most WEBUI_INPUTLOG_BODY_MAX bytes.
 *
 * Pure file I/O and parsing, no globals, no allocation (host-tested in
 * payload/tests/webui_inputlog_selftest.c). Callers serialize appends to one path themselves. */
#ifndef PS5UPLOAD_WEBUI_INPUTLOG_H
#define PS5UPLOAD_WEBUI_INPUTLOG_H

#include <stddef.h>

#define WEBUI_INPUTLOG_PATH "/data/ps5upload/webui-input.log"
#define WEBUI_INPUTLOG_CAP (64u * 1024u)   /* the file's ceiling */
#define WEBUI_INPUTLOG_BODY_MAX 4096u      /* one request's body */
#define WEBUI_INPUTLOG_LINE_MAX 160u       /* one event's line, newline excluded */
#define WEBUI_INPUTLOG_EVENTS_MAX 256u     /* events in one request */

/* `arr` (n bytes, not NUL-terminated) is a JSON array as webui_json_array returns it. Every
 * element must be a plain string: 1..WEBUI_INPUTLOG_LINE_MAX bytes of printable ASCII (0x20-0x7e)
 * without a quote or backslash (so no escapes: a line can never carry a newline and forge
 * another), at most WEBUI_INPUTLOG_EVENTS_MAX of them. Writes each as "<string>\n" to `out`.
 * Returns the bytes written (0 for an empty array), or -1: an element breaks a rule, the array
 * is malformed or has trailing bytes, or `out` (cap bytes) is too small. */
long webui_inputlog_lines(const char *arr, size_t n, char *out, size_t cap);

/* Appends `add` (whole lines) to the ring file `path`. While the result stays within `cap` it is
 * a plain O_APPEND write. Otherwise the file is rewritten to its newest whole lines plus `add`,
 * totalling at most cap - cap/4 (the slack makes rewrites rare), through `<path>.tmp` in the same
 * directory, fsync, then rename() over `path` within that directory (never across mounts); when
 * `add` alone is that large only its own newest whole lines are kept. `work` (work_cap bytes, at
 * least `cap`) is the scratch for a rewrite; a smaller one refuses the rewrite. A failure leaves
 * the previous file whole and no temp file behind. 0, or -1. */
int webui_inputlog_append(const char *path, const char *add, size_t add_len, size_t cap, char *work, size_t work_cap);

/* The route's rate gate. A request is admitted when at least `gap_ms` passed since the last
 * admitted one. A request the page marks final ({"final":true}: the last batch as the Controls
 * screen closes or the page is hidden) may come sooner, but only one final per `gap_ms` window,
 * so the mark cannot be used to flood: at most two appends per window, each body still capped at
 * WEBUI_INPUTLOG_BODY_MAX. Pure (the caller holds its lock and passes a monotonic clock).
 * 1 = admitted (the gate is updated), 0 = refused (429; the gate is unchanged). */
typedef struct {
    long long last_ms;       /* last admitted request */
    long long last_final_ms; /* last admitted request that used the final exemption */
} webui_inputlog_gate;

#define WEBUI_INPUTLOG_GATE_INIT { -1000000, -1000000 }

int webui_inputlog_gate_admit(webui_inputlog_gate *g, long long now_ms, int final, long long gap_ms);

#endif
