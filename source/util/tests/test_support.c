#include "test_support.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mc4/aes.h"
#include "mc4/base64.h"

static const uint8_t MC4_AES256CBC_KEY[] = "304c6528f659c766110239a51cl5dd9c";
static const uint8_t MC4_AES256CBC_IV[] = "u@}kzW2u[u(8DWar";

int onion_test_write_temp_file(const char *suffix, const void *data, size_t len,
                               char *path_out, size_t path_out_size) {
  size_t suffix_len = 0;
  int fd = -1;

  if (path_out == NULL || path_out_size == 0 || suffix == NULL) {
    return -1;
  }
  suffix_len = strlen(suffix);
  if (path_out_size <= sizeof("/tmp/onion-test-XXXXXX") + suffix_len) {
    return -1;
  }

  snprintf(path_out, path_out_size, "/tmp/onion-test-XXXXXX%s", suffix);
  fd = mkstemps(path_out, (int)suffix_len);
  if (fd < 0) {
    return -1;
  }
  if (len > 0 && write(fd, data, len) != (ssize_t)len) {
    close(fd);
    unlink(path_out);
    return -1;
  }
  close(fd);
  return 0;
}

int onion_test_write_temp_text_file(const char *suffix, const char *text,
                                    char *path_out, size_t path_out_size) {
  if (text == NULL) {
    return -1;
  }
  return onion_test_write_temp_file(suffix, text, strlen(text), path_out,
                                    path_out_size);
}

int onion_test_write_temp_mc4_file(const char *xml, char *path_out,
                                   size_t path_out_size) {
  size_t plain_len = 0;
  size_t padded_len = 0;
  uint8_t *cipher = NULL;
  unsigned char *encoded = NULL;
  size_t encoded_len = 0;
  struct AES_ctx ctx;
  int rc = -1;

  if (xml == NULL) {
    return -1;
  }
  plain_len = strlen(xml);
  padded_len = ((plain_len + 15) / 16) * 16;
  cipher = (uint8_t *)calloc(padded_len, 1);
  if (cipher == NULL) {
    return -1;
  }
  memcpy(cipher, xml, plain_len);

  AES_init_ctx_iv(&ctx, MC4_AES256CBC_KEY, MC4_AES256CBC_IV);
  AES_CBC_encrypt_buffer(&ctx, cipher, padded_len);

  encoded = base64_encode(cipher, padded_len, &encoded_len);
  if (encoded == NULL) {
    free(cipher);
    return -1;
  }

  rc = onion_test_write_temp_file(".mc4", encoded, encoded_len, path_out,
                                  path_out_size);
  free(encoded);
  free(cipher);
  return rc;
}

void onion_test_remove_file(const char *path) {
  if (path != NULL && path[0] != '\0') {
    unlink(path);
  }
}

int onion_test_fixture_path(const char *rel, char *out, size_t out_size) {
  const char *root = getenv("ONION_TEST_ROOT");
  if (rel == NULL || out == NULL || out_size == 0) {
    return -1;
  }

  /* 1) Explicit root (Makefile sets ONION_TEST_ROOT). */
  if (root != NULL && root[0] != '\0') {
    snprintf(out, out_size, "%s/%s", root, rel);
    if (access(out, R_OK) == 0)
      return 0;
  }

#ifdef ONION_TEST_DIR
  /* 2) Compile-time tests directory (absolute, cwd-independent). */
  snprintf(out, out_size, "%s/%s", ONION_TEST_DIR, rel);
  if (access(out, R_OK) == 0)
    return 0;
#endif

  /* 3) cwd-relative (make test cds into tests/). */
  snprintf(out, out_size, "%s", rel);
  if (access(out, R_OK) == 0)
    return 0;

  /* 4) Common when binary is run from repo root. */
  snprintf(out, out_size, "source/util/tests/%s", rel);
  if (access(out, R_OK) == 0)
    return 0;

  return -1;
}
