#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cr_cheat_profiles.h"
#include "cr_cheats.h"
#include "cr_game_monitor.h"
#include "cr_log.h"
#include "cr_notifications.h"
#include "cr_paths.h"
#include "cr_titles.h"
#include "third_party/cJSON.h"
}

namespace {

struct CheatProfile {
  std::string titleId;
  std::vector<std::string> mods;
};

class CheatProfileStore {
public:
  static CheatProfileStore &instance() {
    static CheatProfileStore inst;
    return inst;
  }

  void load() {
    std::lock_guard<std::mutex> lk(lock_);
    profiles_.clear();
    char *txt = nullptr;
    if (read_file_text(CHEATRUNNER_CHEAT_PROFILES_PATH, &txt) != 0 || !txt) {
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
      cJSON *mods_j = cJSON_GetObjectItem(item, "mods");
      if (!cJSON_IsString(tid_j) || !tid_j->valuestring || !cJSON_IsArray(mods_j)) {
        continue;
      }
      char norm[10] = {0};
      if (!title_id_normalize(tid_j->valuestring, norm)) {
        continue;
      }
      CheatProfile p;
      p.titleId = norm;
      cJSON *m = nullptr;
      cJSON_ArrayForEach(m, mods_j) {
        if (cJSON_IsString(m) && m->valuestring) {
          p.mods.emplace_back(m->valuestring);
        }
      }
      if (!p.mods.empty()) {
        profiles_.push_back(std::move(p));
        loaded++;
      }
    }
    cJSON_Delete(root);
    if (loaded > 0) {
      cr_log("info", "cheat_profiles", "loaded %d autoload profile(s) from disk", loaded);
    }
  }

  void save(const std::string &titleId, const std::vector<std::string> &mods) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = findLocked(titleId);
    if (mods.empty()) {
      if (it != profiles_.end()) {
        profiles_.erase(it);
      }
    } else if (it != profiles_.end()) {
      it->mods = mods;
    } else {
      CheatProfile p;
      p.titleId = titleId;
      p.mods = mods;
      profiles_.push_back(std::move(p));
    }
    saveLocked();
  }

  bool getMods(const std::string &titleId, std::vector<std::string> &out) {
    std::lock_guard<std::mutex> lk(lock_);
    auto it = findLocked(titleId);
    if (it == profiles_.end()) {
      return false;
    }
    out = it->mods;
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
  std::vector<CheatProfile>::iterator findLocked(const std::string &titleId) {
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
      cJSON *arr = cJSON_AddArrayToObject(item, "mods");
      for (const auto &name : p.mods) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(name.c_str()));
      }
      cJSON_AddItemToArray(root, item);
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
      return;
    }
    write_file_atomic(CHEATRUNNER_CHEAT_PROFILES_PATH, (const uint8_t *)txt, strlen(txt));
    free(txt);
  }

  std::vector<CheatProfile> profiles_;
  std::mutex lock_;
};

/* apply_cheat_json's cooldown/stability gates are shared, so retry per mod. */
void
autoloadWorker(std::string titleId, pid_t pid, std::vector<std::string> mods) {
  const int kRetryMs = 1000;
  const int kPaceMs = 1000; /* gap after every toggle */
  const uint64_t kDeadlineMs = 45000;
  uint64_t started = now_ms();

  for (const auto &name : mods) {
    for (;;) {
      if (now_ms() - started > kDeadlineMs) {
        cr_log("warn", "cheat_profiles",
               "autoload gave up after %llums title=%s remaining_mod='%s'",
               (unsigned long long)kDeadlineMs, titleId.c_str(), name.c_str());
        return;
      }

      running_game_state_t st;
      running_state_get(&st);
      char norm[10] = {0};
      if (!st.running || st.pid != pid ||
          !title_id_normalize(st.title_id, norm) || titleId != norm) {
        cr_log("info", "cheat_profiles", "autoload aborted (game changed/exited) title=%s",
               titleId.c_str());
        return;
      }

      int idx = cheat_find_mod_index_by_name(titleId.c_str(), name.c_str());
      if (idx < 0) {
        cr_log("warn", "cheat_profiles",
               "autoload: mod '%s' not found in current cheat file title=%s — skipping",
               name.c_str(), titleId.c_str());
        break;
      }

      char err[256] = {0};
      int rc = apply_cheat_json(titleId.c_str(), idx, 1, err, sizeof(err));
      if (rc == 0) {
        cr_log("info", "cheat_profiles", "autoload: enabled '%s' title=%s", name.c_str(), titleId.c_str());
        usleep((useconds_t)kPaceMs * 1000);
        break;
      }
      bool retryable = (rc == -2) || !strcmp(err, "app_not_stable");
      if (!retryable) {
        cr_log("warn", "cheat_profiles", "autoload: mod '%s' failed title=%s: %s",
               name.c_str(), titleId.c_str(), err);
        usleep((useconds_t)kPaceMs * 1000);
        break;
      }
      usleep((useconds_t)kRetryMs * 1000);
    }
  }
}

} // namespace

extern "C" void
cheat_profiles_load(void) {
  CheatProfileStore::instance().load();
}

extern "C" int
cheat_profiles_save(const char *title_id, const char *const *mod_names, int count) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return -1;
  }
  std::vector<std::string> mods;
  for (int i = 0; i < count; i++) {
    if (mod_names[i] && mod_names[i][0]) {
      mods.emplace_back(mod_names[i]);
    }
  }
  CheatProfileStore::instance().save(norm, mods);
  return 0;
}

extern "C" int
cheat_profiles_get_json(const char *title_id, char *out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return -1;
  }
  char norm[10] = {0};
  std::vector<std::string> mods;
  if (title_id && title_id_normalize(title_id, norm)) {
    CheatProfileStore::instance().getMods(norm, mods);
  }
  cJSON *arr = cJSON_CreateArray();
  if (!arr) {
    snprintf(out, out_sz, "[]");
    return -1;
  }
  for (const auto &name : mods) {
    cJSON_AddItemToArray(arr, cJSON_CreateString(name.c_str()));
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
cheat_profiles_clear(const char *title_id) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return;
  }
  CheatProfileStore::instance().clear(norm);
}

extern "C" int
cheat_profiles_has(const char *title_id) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return 0;
  }
  return CheatProfileStore::instance().has(norm) ? 1 : 0;
}

extern "C" void
cheat_profiles_autoload_for_title(const char *title_id, pid_t pid) {
  char norm[10] = {0};
  if (!title_id || !title_id_normalize(title_id, norm)) {
    return;
  }
  std::vector<std::string> mods;
  if (!CheatProfileStore::instance().getMods(norm, mods) || mods.empty()) {
    return;
  }
  cr_log("info", "cheat_profiles", "autoload profile found (%zu mod(s)) title=%s — applying",
         mods.size(), norm);
  notify("CheatRunner: Auto-loading (%zu) cheats for %s", mods.size(), norm);
  autoloadWorker(std::string(norm), pid, std::move(mods));
}
