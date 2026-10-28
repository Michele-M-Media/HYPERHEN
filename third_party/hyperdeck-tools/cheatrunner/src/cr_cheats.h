#ifndef CR_CHEATS_H
#define CR_CHEATS_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>

typedef struct cJSON cJSON;

#define MAX_CHEAT_CANDIDATES 64

/* cheat_candidate_entry_t.match values */
#define CAND_MATCH_EXACT         0  /* filename version matches game version */
#define CAND_MATCH_WRONG_VERSION 1  /* filename has version but it doesn't match */
#define CAND_MATCH_GENERIC       2  /* no version in filename */
#define CAND_MATCH_UNKNOWN       3  /* game version unknown, can't classify */

/* cheat_file_search_t.selection_kind values */
#define CHEAT_SEL_NONE          0  /* no candidate selected */
#define CHEAT_SEL_EXACT         1  /* selected via exact version match */
#define CHEAT_SEL_GENERIC       2  /* selected via generic (no-version) file */
#define CHEAT_SEL_WRONG_VERSION 3  /* manual override only */
#define CHEAT_SEL_MANUAL        4  /* manually overridden by user */

typedef struct cheat_candidate_entry {
  char path[256];
  char version[32];  /* raw version extracted from filename, or "" */
  int kind;   /* 1=json, 2=shn, 3=mc4 */
  int score;
  int match;  /* CAND_MATCH_* */
} cheat_candidate_entry_t;

typedef struct cheat_file_search {
  const char *want;
  char want_buf[10];
  char preferred_ver[32];
  char best_path[256];
  int best_kind;
  int best_score;
  int selection_kind;  /* CHEAT_SEL_* */
  int log_candidates;
  int candidate_count;
  cheat_candidate_entry_t candidates[MAX_CHEAT_CANDIDATES];
} cheat_file_search_t;

typedef struct { char title_id[10]; pid_t pid; int mod_index; } mod_disabled_rec_t;

typedef struct {
  char title_id[10];
  int mod_index;
  char mod_name[64];
  pid_t pid;
  uint64_t enabled_at_ms;
} crash_guard_state_t;

typedef struct {
  char title_id[10];
  int mod_index;
  char mod_name[64];
  pid_t pid;
  uint64_t elapsed_ms;
  time_t ts;
  int app_id;
  int hook_codecave;
} crash_suspect_rec_t;

typedef struct {
  char title_id[16];
  int app_id;
  pid_t pid;
  char reason[32];
  uint64_t ts_ms;
  uint64_t elapsed_after_last_cheat_ms;
  int last_mod_index;
  char last_mod_name[64];
  int suspected;
} last_game_exit_t;

#define LAST_APPLY_WRITES_MAX 64
typedef struct {
  char title_id[16];
  int mod_index;
  char mod_name[64];
  int effective_on;
  int ok;
  int hook_codecave;
  int cave_count;
  int hook_count;
  struct { intptr_t addr; size_t len; int is_cave; } writes[LAST_APPLY_WRITES_MAX];
  int write_count;
  uint64_t ts_ms;
} last_apply_rec_t;

#define MOD_DISABLED_MAX 256
#define CRASH_GUARD_MAX 8
#define CRASH_SUSPECT_MAX 32

extern mod_disabled_rec_t g_mods_disabled[MOD_DISABLED_MAX];
extern int g_mods_disabled_n;
extern pthread_mutex_t g_mods_disabled_lock;

extern crash_guard_state_t g_crash_guard_arr[CRASH_GUARD_MAX];
extern int g_crash_guard_n;
extern crash_suspect_rec_t g_crash_suspects[CRASH_SUSPECT_MAX];
extern int g_crash_suspects_n;
extern pthread_mutex_t g_crash_guard_lock;

extern pthread_mutex_t g_last_game_exit_lock;
extern last_game_exit_t g_last_game_exit;
extern pthread_mutex_t g_last_apply_lock;
extern last_apply_rec_t g_last_apply_rec;
extern _Atomic int g_cheat_applying;
/* Held by cheat-apply/patch-apply/patch-restore so only one runs at a time. */
extern pthread_mutex_t g_cheat_apply_lock;
extern volatile uint64_t g_last_apply_at_ms;
extern volatile uint64_t g_post_apply_guard_until_ms;

int find_cheat_file_for_title(const char *title_id, char *out, size_t out_size, int *kind_out);
int cheat_find_mod_index_by_name(const char *title_id, const char *mod_name);
int find_cheat_candidates(const char *title_id, cheat_file_search_t *ctx_out);
/* Like find_cheat_candidates but uses override_ver (if non-NULL/non-empty) instead of static SFO lookup. */
int find_cheat_candidates_ex(const char *title_id, const char *override_ver, cheat_file_search_t *ctx_out);

/* Manual cheat file selection — persisted to /data/cheatrunner/cheat_selections.json */
int  cheat_manual_select(const char *title_id, const char *path);
void cheat_manual_select_clear(const char *title_id);
int  cheat_manual_get(const char *title_id, char *out, size_t out_sz);
int mod_disabled_check(const char *tid, pid_t pid, int mod);
int mod_enabled_check(const char *tid, pid_t pid, int mod);
void mod_enabled_clear_for_pid(pid_t pid);
int cheat_any_enabled_for_title(const char *title_id, pid_t pid);
void fmt_hex16(const uint8_t *b, size_t len, char *buf, size_t buf_sz);
int apply_cheat_json(const char *title_id, int mod_index, int turn_on, char *err, size_t err_size);

/* Master-code-dependent address resolution — shared between apply and state-read paths. */
cJSON *find_master_code_mod_for(cJSON *mods, int target_mod_idx);
uint64_t mc_mod_first_offset(cJSON *mc_mod);
int mc_scan_dep_addr(pid_t pid, intptr_t mc_addr,
                      const uint8_t *mc_on, size_t mc_on_len,
                      const uint8_t *dep_off, size_t dep_off_len,
                      uint64_t mc_base_off, uint64_t dep_raw_off,
                      intptr_t *addr_out);

/* Persist crash suspects to / from disk so they survive CheatRunner restarts */
void crash_suspects_save(void);
void crash_suspects_load(void);

#endif

