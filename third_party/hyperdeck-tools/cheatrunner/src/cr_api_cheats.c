#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_cheats.h"

int
cr_api_cheats_handle(int fd, const char *method, const char *path,
                      const char *query, const char *body, size_t body_len) {
  int is_post = !strcmp(method, "POST");

  if (!strcmp(path, "/api/cheats")) {
    if (query && strstr(query, "titleId=")) {
      handle_api_cheats_get(fd, query);
    } else {
      handle_api_cheats_index(fd);
    }
    return 1;
  }
  if (!strcmp(path, "/api/cheats/toggle")) {
    handle_api_cheats_toggle(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/apply-dryrun")) {
    handle_api_cheats_apply_dryrun(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/state")) {
    handle_api_cheats_state(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/debug")) {
    handle_api_cheats_debug(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/engine")) {
    handle_api_cheats_engine(fd);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/engine/toggle")) {
    handle_api_cheats_engine_toggle(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/delete")) {
    handle_api_cheats_delete(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/find")) {
    handle_api_cheats_find(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/address-debug")) {
    handle_api_cheats_address_debug(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/download")) {
    handle_api_cheats_download(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/list")) {
    handle_api_cheats_list(fd);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/download-all")) {
    handle_api_cheats_download_all(fd);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/download-all/status")) {
    handle_api_cheats_download_all_status(fd);
    return 1;
  }
  /* /api/cheats/repo/download?source=<hencollection|ps5cheats|goldhen|all>&overwrite=0|1 */
  if (!strcmp(path, "/api/cheats/repo/download")) {
    handle_api_cheats_repo_download(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/repo/download/status")) {
    handle_api_cheats_repo_download_status(fd);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/raw")) {
    handle_api_cheats_raw(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/validate")) {
    handle_api_cheats_validate(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/upload") || !strcmp(path, "/api/cheats/save")) {
    if (!is_post) {
      http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\",\"message\":\"Use POST.\"}");
      return 1;
    }
    handle_api_cheats_upload(fd, query, body ? body : "", body_len);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/index/status")) {
    http_send_json(fd, 410, "{\"ok\":false,\"error\":\"legacy_endpoint_removed\"}");
    return 1;
  }
  if (!strcmp(path, "/api/cheats/index/refresh")) {
    http_send_json(fd, 410, "{\"ok\":false,\"error\":\"legacy_endpoint_removed\"}");
    return 1;
  }
  if (!strcmp(path, "/api/cheats/scan")) {
    handle_api_cheats_scan(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/mc4-debug")) {
    handle_api_cheats_mc4_debug(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/clear-crash-flags")) {
    handle_api_cheats_clear_crash_flags(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/select")) {
    if (!is_post) {
      http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\",\"message\":\"Use POST.\"}");
      return 1;
    }
    handle_api_cheats_select(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/cheats/select/auto")) {
    if (!is_post) {
      http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\",\"message\":\"Use POST.\"}");
      return 1;
    }
    handle_api_cheats_select_auto(fd, query);
    return 1;
  }
  /* /api/cheats/disable-all?titleId=<id> — revert all active cheats for running game */
  if (!strcmp(path, "/api/cheats/disable-all")) {
    handle_api_cheats_disable_all(fd, query);
    return 1;
  }
  return 0;
}
