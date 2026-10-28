#ifndef CR_API_DASHBOARD_H
#define CR_API_DASHBOARD_H

#include <stddef.h>

int cr_api_dashboard_handle(int fd, const char *method, const char *path,
                             const char *query, const char *body, size_t body_len,
                             int accepts_gzip);

#endif /* CR_API_DASHBOARD_H */
