/* The console's user-visible firmware version ("9.60", "13.60") from its kernel build
 * string (sysctl kern.version) — the same string the helper's STATUS frame reports as
 * `ps5_kernel`, parsed the way the app parses it (client/src/lib/ps5Firmware.ts):
 *
 *   FreeBSD 11.0-RELEASE-p0 #1 r218215/releases/09.60: Jul 18 2023  ->  9.60
 *
 * Patterns, most specific first: "releases/NN.NN"; then "/NN.NN" followed by '-', a
 * space or ':'; then any NN.NN standing alone (the FreeBSD "11.0" never matches: it has
 * one digit after the point). The major loses a leading zero, as the console shows it.
 * A string that matches none is reported whole (one line, printable ASCII: node.info's
 * field is UTF-8), so the version is never silently empty; an empty string is "unknown".
 *
 * Header-only and libc-only: built into the payload and, for tests, on the host. */
#ifndef PS5_FIRMWARE_H
#define PS5_FIRMWARE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static inline int ps5_fw_digit(char c) { return c >= '0' && c <= '9'; }

/* A version at s: 1-2 digits, '.', exactly 2 digits. Writes "M.mm"/"MM.mm" (no leading
 * zero) to out and returns 1; 0 when s does not start one. */
static inline int ps5_fw_at(const char *s, char *out, size_t cap) {
    size_t n = 0;
    unsigned major = 0;
    while (n < 2 && ps5_fw_digit(s[n])) major = major * 10u + (unsigned)(s[n++] - '0');
    if (n == 0 || ps5_fw_digit(s[n]) || s[n] != '.') return 0;
    if (!ps5_fw_digit(s[n + 1]) || !ps5_fw_digit(s[n + 2]) || ps5_fw_digit(s[n + 3])) return 0;
    snprintf(out, cap, "%u.%c%c", major, s[n + 1], s[n + 2]);
    return 1;
}

static inline void ps5_firmware_from_kernel(const char *kv, char *out, size_t cap) {
    const char *p;
    size_t i, n;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!kv || !kv[0]) {
        snprintf(out, cap, "unknown");
        return;
    }
    /* 1. releases/NN.NN */
    for (p = strstr(kv, "releases/"); p; p = strstr(p + 1, "releases/"))
        if (ps5_fw_at(p + 9, out, cap)) return;
    /* 2. /NN.NN followed by '-', ' ' or ':' */
    for (p = strchr(kv, '/'); p; p = strchr(p + 1, '/')) {
        char tmp[16];
        const char *q = p + 1;
        size_t d = 0;
        while (d < 2 && ps5_fw_digit(q[d])) d++;
        if (ps5_fw_at(q, tmp, sizeof tmp) && (q[d + 3] == '-' || q[d + 3] == ' ' || q[d + 3] == ':')) {
            snprintf(out, cap, "%s", tmp);
            return;
        }
    }
    /* 3. a bare NN.NN with no digit before it */
    for (i = 0; kv[i]; i++)
        if ((i == 0 || !ps5_fw_digit(kv[i - 1])) && ps5_fw_at(kv + i, out, cap)) return;
    /* None: the string itself, one line, trailing whitespace dropped. */
    n = strlen(kv);
    while (n > 0 && (kv[n - 1] == '\n' || kv[n - 1] == '\r' || kv[n - 1] == ' ')) n--;
    if (n > cap - 1) n = cap - 1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)kv[i];
        out[i] = (c == '\n' || c == '\r') ? ' ' : (c < 0x20 || c >= 0x7f) ? '?' : (char)c;
    }
    out[n] = '\0';
}

#endif
