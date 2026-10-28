#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "cr_version.h"

int
cr_version_normalize(const char *in, char *out, size_t out_sz) {
  if (!out || out_sz < 2) {
    if (out && out_sz) out[0] = '\0';
    return 0;
  }
  out[0] = '\0';
  if (!in || !*in) return 0;

  /* Strip leading 'v' or 'V' */
  while (*in == 'v' || *in == 'V') in++;
  if (!*in) return 0;

  /* Parse dot-separated numeric segments */
  int segs[16];
  int nseg = 0;
  const char *p = in;
  while (*p && nseg < 16) {
    if (!isdigit((unsigned char)*p)) break;
    int v = 0;
    while (*p && isdigit((unsigned char)*p)) {
      v = v * 10 + (*p++ - '0');
    }
    segs[nseg++] = v;
    if (*p == '.') {
      p++;
    } else {
      break;
    }
  }
  if (nseg == 0) return 0;

  /* Strip trailing all-zero segments (but always keep at least 1) */
  while (nseg > 1 && segs[nseg - 1] == 0) nseg--;

  /* Build output */
  size_t pos = 0;
  for (int i = 0; i < nseg; i++) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), i == 0 ? "%d" : ".%d", segs[i]);
    if (n <= 0 || pos + (size_t)n >= out_sz) return 0;
    memcpy(out + pos, buf, (size_t)n);
    pos += (size_t)n;
  }
  out[pos] = '\0';
  return pos > 0 ? 1 : 0;
}

int
cr_version_equal(const char *a, const char *b) {
  char na[32], nb[32];
  int ha = cr_version_normalize(a, na, sizeof(na));
  int hb = cr_version_normalize(b, nb, sizeof(nb));
  if (!ha && !hb) return 1; /* both empty/unparseable — treat as equal */
  if (!ha || !hb) return 0;
  return strcmp(na, nb) == 0 ? 1 : 0;
}

int
cr_version_compare(const char *a, const char *b) {
  char na[32], nb[32];
  cr_version_normalize(a, na, sizeof(na));
  cr_version_normalize(b, nb, sizeof(nb));
  const char *pa = na, *pb = nb;
  for (;;) {
    if (!*pa && !*pb) return 0;
    int ia = 0, ib = 0;
    while (*pa && *pa != '.') { ia = ia * 10 + (*pa++ - '0'); }
    while (*pb && *pb != '.') { ib = ib * 10 + (*pb++ - '0'); }
    if (ia != ib) return ia - ib;
    if (*pa == '.') pa++;
    if (*pb == '.') pb++;
  }
}

int
cr_version_equal_known(const char *a, const char *b) {
  char na[32], nb[32];
  int ha = cr_version_normalize(a, na, sizeof(na));
  int hb = cr_version_normalize(b, nb, sizeof(nb));
  if (!ha || !hb) return 0;
  return strcmp(na, nb) == 0 ? 1 : 0;
}

int
cr_version_from_filename(const char *name, char *out, size_t out_sz) {
  if (!name || !out || out_sz < 2) return 0;
  out[0] = '\0';
  const char *us = strchr(name, '_');
  if (!us) return 0;
  us++;
  if (!isdigit((unsigned char)*us)) return 0;
  const char *end = us;
  while (*end && (isdigit((unsigned char)*end) || *end == '.')) end++;
  size_t len = (size_t)(end - us);
  /* strip trailing dots (e.g. "01.14." from "CUSA42556_01.14.json") */
  while (len > 0 && us[len - 1] == '.') len--;
  if (!len || len >= out_sz) return 0;
  memcpy(out, us, len);
  out[len] = '\0';
  return 1;
}
