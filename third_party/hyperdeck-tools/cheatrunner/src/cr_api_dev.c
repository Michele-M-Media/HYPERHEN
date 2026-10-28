#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_dev.h"

int
cr_api_dev_handle(int fd, const char *method, const char *path,
                   const char *query, const char *body, size_t body_len) {
  (void)method;
  (void)body;
  (void)body_len;
  if (!strcmp(path, "/api/config")) {
    handle_api_config(fd);
    return 1;
  }
  if (!strcmp(path, "/api/config/set")) {
    handle_api_config_set(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/config/reset")) {
    handle_api_config_reset(fd);
    return 1;
  }
  if (!strcmp(path, "/api/config/preset")) {
    handle_api_config_preset(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/dev/privileges")) {
    handle_api_dev_privileges(fd);
    return 1;
  }
  if (!strcmp(path, "/api/dev/memtest")) {
    handle_api_dev_memtest(fd);
    return 1;
  }
  if (!strcmp(path, "/api/dev/diag")) {
    handle_api_dev_diag(fd);
    return 1;
  }
  if (!strcmp(path, "/api/dev/open-browser")) {
    handle_api_dev_open_browser(fd, method, query);
    return 1;
  }
  /* /api/dev/shutdown is handled directly in http_route (needs client_ip) */
  return 0;
}
