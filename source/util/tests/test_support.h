#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int onion_test_write_temp_file(const char *suffix, const void *data, size_t len,
                               char *path_out, size_t path_out_size);
int onion_test_write_temp_text_file(const char *suffix, const char *text,
                                    char *path_out, size_t path_out_size);
/** Encrypt XML as MC4 (Base64 + AES-256-CBC), write temp .mc4. */
int onion_test_write_temp_mc4_file(const char *xml, char *path_out,
                                   size_t path_out_size);
void onion_test_remove_file(const char *path);

/** Resolve fixture path under tests/fixtures (cwd or ONION_TEST_ROOT). */
int onion_test_fixture_path(const char *rel, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
