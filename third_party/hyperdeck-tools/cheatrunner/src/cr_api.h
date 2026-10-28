#ifndef CR_API_H
#define CR_API_H

#include <stddef.h>

void http_route(int fd, const char *method, const char *path, const char *query,
                const char *client_ip, const char *body, size_t body_len,
                int accepts_gzip);

#endif
