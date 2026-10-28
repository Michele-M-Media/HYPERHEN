#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_sources.h"

int
cr_api_sources_handle(int fd, const char *method, const char *path,
                       const char *query, const char *body, size_t body_len) {
  (void)method; (void)query; (void)body; (void)body_len;

  if (!strcmp(path, "/api/cheats/repo/download")) {
    handle_api_cheats_repo_download(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/repo/download/status")) {
    handle_api_cheats_repo_download_status(fd);
    return 1;
  }
  return 0;
}
