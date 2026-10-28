#ifndef CR_JSON_H
#define CR_JSON_H

#include <stddef.h>
#include "third_party/cJSON.h"

char *json_escape(const char *src);

int json_extract_string(const char *json, const char *key, char *out, size_t out_size);
int cjson_copy_string(cJSON *item, char *out, size_t out_size);
int cjson_find_string_recursive(cJSON *node, const char *key, char *out, size_t out_size, int depth);
int cjson_read_localized_title(cJSON *root, char *out, size_t out_size);

#endif /* CR_JSON_H */