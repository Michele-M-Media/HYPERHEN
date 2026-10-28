#ifndef CR_VERSION_H
#define CR_VERSION_H

#include <stddef.h>

/* Canonical form: drop a leading 'v', leading zeros per segment and trailing .0 segments ("01.09.00" -> "1.9").
 * Returns 1 on success, 0 if the input is empty/unparseable (out is set to ""). */
int cr_version_normalize(const char *in, char *out, size_t out_sz);

/* 1 if a and b represent the same version (after normalization), 0 otherwise. */
int cr_version_equal(const char *a, const char *b);

/* >0 if a > b, <0 if a < b, 0 if equal (all after normalization). */
int cr_version_compare(const char *a, const char *b);

/* 1 if both inputs normalize successfully AND are equal. unknown/unknown → 0. */
int cr_version_equal_known(const char *a, const char *b);

/* 1 if the input normalizes to at least one numeric segment. */

/* Extract version token after the first '_' in a cheat filename.
 * "CUSA00900_01.09.json" → "01.09".  Returns 1 on success, 0 if not found. */
int cr_version_from_filename(const char *name, char *out, size_t out_sz);

#endif /* CR_VERSION_H */
