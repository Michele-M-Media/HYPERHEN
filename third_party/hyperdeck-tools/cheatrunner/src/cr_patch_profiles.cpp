#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cr_api_patches.h"
#include "cr_game_monitor.h"
#include "cr_log.h"
#include "cr_notifications.h"
#include "cr_patch_profiles.h"
#include "cr_paths.h"
#include "cr_titles.h"
#include "third_party/cJSON.h"
}

namespace {

struct PatchProfile {
  std::string titleId;
  std::vector<std::string> entryIds;
};

class PatchProfileStore {
public:
  static PatchProfileStore &instance() {
    static PatchProfileStore inst;
    return inst;
  }

  void load() {
    std::lock_guard<std::mutex> lk(lock_);
    profiles_.clear();
    char *txt = nullptr;
    if (read_file_text(CHEATRUNNER_PATCH_PROFILES_PATH, &txt) != 0 || !txt) {
      return;
    }
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    if (!root || !cJSON_IsArray(root)) {
      cJSON_Delete(root);
      return;
    }
    int loaded = 0;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
      cJSON *tid_j  = cJSON_GetObjectItem(item, "titleId");
      cJSON *eids_j = cJSON_GetObjectItem(item, "entryIds");
      if (!cJSON_IsString(tid_j) || !tid_j->valuestring || !cJSON_IsArray(eids_j)) {
        continue;
      }
      char norm[10] = {0};
      if (!title_id_normalize(tid_j->valuestring, norm)) {
        continue;
      }
      PatchProfile p;
      p.titleId = norm;
      cJSON *m = nullptr;
      cJSON_ArrayForEach(m, eids_j) {
        if (cJSON_IsString(m) && m->valuestring) {
          p.entryIds.emplace_back(m->valuestring);
        }
      }
      if (!p.entryIds.empty()) {
        profiles_.push_back(std::move(p));
        loaded++;
      }
    }
    cJSON_Delete(root);
    if (loaded > 0) {
      cr_log("info", "patch_profiles", "loaded %d autoload profile(s) from disk", loaded);
    }
  }

  void save(const std::string &titleId, const std::vector<std::string> &entryIds) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = findLocked(titleId);
    if (entryIds.empty()) {
      if (it != profiles_.end()) {
        profiles_.erase(it);
      }
    } else if (it != profiles_.end()) {
      it->entryIds = entryIds;
    } else {
      PatchProfile p;
      p.titleId = titleId;
      p.entryIds = entryIds;
      profiles_.push_back(std::move(p));
    }
    saveLocked();
  }

  bool getEntryIds(const std::string &titleId, std::vector<std::string> &out) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = findLocked(titleId);
    if (it == profiles_.end()) {
      return false;
    }
    out = it->entryIds;
    return true;
  }

  void clear(const std::string &titleId) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = findLocked(titleId);
    if (it != profiles_.end()) {
      profiles_.erase(it);
      saveLocked();
    }
  }

  bool has(const std::string &titleId) {
    std::lock_guard<std::mutex> lk(lock_);
    return findLocked(titleId) != profiles_.end();
  }

private:
  std::vector<PatchProfile>::iterator findLocked(const std::string &titleId) {
    for (auto it = profiles_.begin(); it != profiles_.end(); ++it) {
      if (it->titleId == titleId) {
        return it;
      }
    }
    return profiles_.end();
  }

  void saveLocked() {
    cJSON *root = cJSON_CreateArray();
    if (!root) {
      return;
    }
    for (const auto &p : profiles_) {
      cJSON *item = cJSON_CreateObject();
      if (!item) {
        continue;
      }
      cJSON_AddStringToObject(item, "titleId", p.titleId.c_str());
      cJSON *arr = cJSON_AddArrayToObject(item, "entryIds");
      for (const auto &eid : p.entryIds) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(eid.c_str()));
      }
      cJSON_AddItemToArray(root, item);
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
      return;
    }
    write_file_atomic(CHEATRUNNER_PATCH_PROFILES_PATH, (const uint8_t *)txt, strlen(txt));
    free(txt);
  }

  std::vector<PatchProfile> profiles_;
  std::mutex lock_;
};

/* Keyed by entryId (stable across reloads), not array index. No force-retry on version mismatch. */
void
autoloadWorker(std::string titleId, pid_t pid, std::vector<std::string> entryIds) {
  const int kRetryMs = 1500;
  const uint64_t kDeadlineMs = 45000;
  uint64_t started = now_ms();

  for (const auto &eid : entryIds) {
    for (;;) {
      if (now_ms() - started > kDeadlineMs) {
        cr_log("warn", "patch_profiles",
               "autoload gave up after %llums title=%s remaining_entry=%s",
               (unsigned long long)kDeadlineMs, titleId.c_str(), eid.c_str());
        return;
      }

      running_game_state_t st;
      running_state_get(&st);
      char norm[10] = {0};
      if (!st.running || st.pid != pid ||
          !title_id_normalize(st.title_id, norm) || titleId != norm) {
        cr_log("info", "patch_profiles", "autoload aborted (game changed/exited) title=%s",
               titleId.c_str());
        return;
      }

      char err[64] = {0};
      int rc = patch_apply_by_entry_id(titleId.c_str(), eid.c_str(), 0, err, sizeof(err));
      if (rc == 0) {
        cr_log("info", "patch_profiles", "autoload: applied entry=%s title=%s",
               eid.c_str(), titleId.c_str());
        usleep(1000 * 1000); /* patch_apply has no cooldown of its own */
        break;
      }
      bool permanent = !strcmp(err, "entry_not_found") || !strcmp(err, "no_supported_lines") ||
                        !strcmp(err, "unsupported_line_type") || !strcmp(err, "version_mismatch") ||
                        !strcmp(err, "patch_not_found");
      if (permanent) {
        cr_log("warn", "patch_profiles", "autoload: entry=%s failed title=%s: %s",
               eid.c_str(), titleId.c_str(), err);
        usleep(1000 * 1000);
        break;
      }
      usleep((useconds_t)kRetryMs * 1000);
    }
  }
}

} // namespace

extern "C" void
patch_profiles_load(void) {
  PatchProfileStore::instance().load();
}

extern "C" int
patch_profiles_save(const char *title_id, const char *const *entry_ids, int count) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return -1;
  }
  std::vector<std::string> ids;
  for (int i = 0; i < count; i++) {
    if (entry_ids[i] && entry_ids[i][0]) {
      ids.emplace_back(entry_ids[i]);
    }
  }
  PatchProfileStore::instance().save(norm, ids);
  return 0;
}

extern "C" int
patch_profiles_get_json(const char *title_id, char *out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return -1;
  }
  char norm[10] = {0};
  std::vector<std::string> ids;
  if (title_id && title_id_normalize(title_id, norm)) {
    PatchProfileStore::instance().getEntryIds(norm, ids);
  }
  cJSON *arr = cJSON_CreateArray();
  if (!arr) {
    snprintf(out, out_sz, "[]");
    return -1;
  }
  for (const auto &eid : ids) {
    cJSON_AddItemToArray(arr, cJSON_CreateString(eid.c_str()));
  }
  char *txt = cJSON_PrintUnformatted(arr);
  cJSON_Delete(arr);
  if (!txt) {
    snprintf(out, out_sz, "[]");
    return -1;
  }
  snprintf(out, out_sz, "%s", txt);
  free(txt);
  return 0;
}

extern "C" void
patch_profiles_clear(const char *title_id) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return;
  }
  PatchProfileStore::instance().clear(norm);
}

extern "C" int
patch_profiles_has(const char *title_id) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return 0;
  }
  return PatchProfileStore::instance().has(norm) ? 1 : 0;
}

extern "C" void
patch_profiles_autoload_for_title(const char *title_id, pid_t pid) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return;
  }
  std::vector<std::string> ids;
  if (!PatchProfileStore::instance().getEntryIds(norm, ids) || ids.empty()) {
    return;
  }
  cr_log("info", "patch_profiles", "autoload profile found (%zu entr%s) title=%s — applying",
         ids.size(), ids.size() == 1 ? "y" : "ies", norm);
  notify("CheatRunner: Auto-loading (%zu) patches for %s", ids.size(), norm);
  autoloadWorker(std::string(norm), pid, std::move(ids));
}
