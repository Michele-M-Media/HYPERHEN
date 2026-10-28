#include <vector>
#include <string>

extern "C" {
#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_patch_profiles.h"
#include "cr_patch_profiles.h"
#include "cr_titles.h"
#include "third_party/cJSON.h"
}

namespace {

void
handleGet(int fd, const char *query) {
  char raw_tid[32] = {0};
  if (query_value(query, "titleId", raw_tid, sizeof(raw_tid)) != 0 || !raw_tid[0]) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing titleId\"}");
    return;
  }
  char title_id[10] = {0};
  if (!title_id_normalize(raw_tid, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid titleId\"}");
    return;
  }
  char ids_json[4096];
  patch_profiles_get_json(title_id, ids_json, sizeof(ids_json));
  char body[4200];
  snprintf(body, sizeof(body), "{\"ok\":true,\"titleId\":\"%s\",\"entryIds\":%s}", title_id, ids_json);
  http_send_json(fd, 200, body);
}

void
handleSave(int fd, const char *query, const char *body, size_t body_len) {
  char raw_tid[32] = {0};
  if (query_value(query, "titleId", raw_tid, sizeof(raw_tid)) != 0 || !raw_tid[0]) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing titleId\"}");
    return;
  }
  char title_id[10] = {0};
  if (!title_id_normalize(raw_tid, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid titleId\"}");
    return;
  }
  (void)body_len;
  cJSON *root = cJSON_Parse(body ? body : "");
  cJSON *ids_j = root;
  if (root && cJSON_IsObject(root)) {
    ids_j = cJSON_GetObjectItem(root, "entryIds");
  }
  if (!cJSON_IsArray(ids_j)) {
    cJSON_Delete(root);
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"body must be a JSON array or {\\\"entryIds\\\":[...]}\"}");
    return;
  }

  std::vector<std::string> ids;
  std::vector<const char *> ptrs;
  cJSON *m = nullptr;
  cJSON_ArrayForEach(m, ids_j) {
    if (cJSON_IsString(m) && m->valuestring && m->valuestring[0]) {
      ids.emplace_back(m->valuestring);
    }
  }
  cJSON_Delete(root);

  ptrs.reserve(ids.size());
  for (const auto &n : ids) {
    ptrs.push_back(n.c_str());
  }
  patch_profiles_save(title_id, ptrs.empty() ? nullptr : ptrs.data(), (int)ptrs.size());

  char resp[64];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"count\":%d}", (int)ptrs.size());
  http_send_json(fd, 200, resp);
}

void
handleClear(int fd, const char *query) {
  char raw_tid[32] = {0};
  if (query_value(query, "titleId", raw_tid, sizeof(raw_tid)) != 0 || !raw_tid[0]) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing titleId\"}");
    return;
  }
  char title_id[10] = {0};
  if (!title_id_normalize(raw_tid, title_id)) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"invalid titleId\"}");
    return;
  }
  patch_profiles_clear(title_id);
  http_send_json(fd, 200, "{\"ok\":true}");
}

} // namespace

extern "C" int
cr_api_patch_profiles_handle(int fd, const char *method, const char *path,
                              const char *query, const char *body, size_t body_len) {
  int is_post = !strcmp(method, "POST");

  if (!strcmp(path, "/api/patches/profile")) {
    handleGet(fd, query);
    return 1;
  }
  if (!strcmp(path, "/api/patches/profile/save")) {
    if (!is_post) {
      http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\"}");
      return 1;
    }
    handleSave(fd, query, body, body_len);
    return 1;
  }
  if (!strcmp(path, "/api/patches/profile/clear")) {
    if (!is_post) {
      http_send_json(fd, 405, "{\"ok\":false,\"error\":\"method_not_allowed\"}");
      return 1;
    }
    handleClear(fd, query);
    return 1;
  }
  return 0;
}
