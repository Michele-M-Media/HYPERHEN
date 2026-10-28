#ifndef CR_SHA1_H
#define CR_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* Git blob id of a buffer: sha1("blob <len>\0" + data) as 40 lowercase hex chars + NUL. */
void cr_git_blob_sha1(const uint8_t *data, size_t len, char out[41]);

#endif /* CR_SHA1_H */
