#pragma once

#include <stddef.h>

int archive_path_supported(const char *path);
int archive_output_path(const char *archive, const char *base, int separate,
                        char *out, size_t out_size);
