/* The file index's name pattern match (runtime.c INDEX_SEARCH): `*` matches any run of bytes, `?`
 * exactly one byte, everything else itself with ASCII letters compared case-insensitively; the whole
 * name must match. Iterative with one backtrack point (the last `*`): O(len(pat) * len(str)) at
 * worst, whatever the pattern (a recursive matcher took ~0.4 s per name on `*?*?*?*?*?*?*?*z`, and
 * the search runs under the index lock). Header-only so the host selftest builds it. */
#ifndef PS5UPLOAD_INDEX_GLOB_H
#define PS5UPLOAD_INDEX_GLOB_H

static inline char index_glob_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* 1 when `str` matches `pat`, else 0. Both NUL-terminated. */
static inline int index_glob_match(const char *pat, const char *str) {
    const char *star = 0;    /* the pattern just past the last '*' seen */
    const char *resume = 0;  /* where in str that '*' would take one more byte */
    while (*str) {
        if (*pat == '*') {
            while (*pat == '*') pat++; /* a run of stars is one star */
            if (!*pat) return 1;
            star = pat;
            resume = str;
            continue;
        }
        if (*pat == '?' || (*pat && index_glob_lower(*pat) == index_glob_lower(*str))) {
            pat++;
            str++;
            continue;
        }
        if (!star) return 0;
        /* mismatch after a star: let the star take one more byte and try again from there */
        pat = star;
        str = ++resume;
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

#endif
