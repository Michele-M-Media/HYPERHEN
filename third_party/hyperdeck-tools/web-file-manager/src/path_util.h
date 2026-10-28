#pragma once

#include <stddef.h>
#include <limits.h>

#include <microhttpd.h>

char *query_value(struct MHD_Connection *conn, const char *key);
char *header_value(struct MHD_Connection *conn, const char *key);
char *request_value(struct MHD_Connection *conn, const char *header,
                    const char *query);
char *form_decode(const char *src, size_t len);
char *body_form_value(const char *body, size_t body_size, const char *key);

void free_paths(char **paths, size_t count);
int parse_paths(const char *raw, char ***out_paths, size_t *out_count);

const char *path_basename(const char *path);
void path_basename_copy(const char *path, char *out, size_t size);
int path_dirname(const char *path, char *out, size_t size);
int path_join(char *out, size_t size, const char *dir, const char *name);
int relative_path_safe(const char *path);
int path_join_relative(char *out, size_t size, const char *dir, const char *rel);
char *fs_path_value(char *path);
