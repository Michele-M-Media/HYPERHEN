#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_logs.h"

int
cr_api_logs_handle(int fd, const char *method, const char *path,
                   const char *query, const char *body, size_t body_len) {
  (void)method; (void)body; (void)body_len;
  if (!strcmp(path, "/api/logs")) {
    handle_api_logs(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/logs/export")) {
    handle_api_logs(fd, "all=1");
    return 1;
  }
  if (!strcmp(path, "/api/logs/clear")) {
    handle_api_logs_clear(fd);
    return 1;
  }
  return 0;
}
