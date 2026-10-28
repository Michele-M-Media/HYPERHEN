#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ps5/kernel.h>

#include "ps5sdk_compat.h"
#include "priv_bootstrap.h"
#include "pt.h"
#include "third_party/cJSON.h"
#include "cr_log.h"
#include "cr_paths.h"
#include "cr_config.h"
#include "cr_activity.h"
#include "cr_notifications.h"
#include "cr_version.h"
#include "cr_titles.h"
#include "cr_game_monitor.h"
#include "cr_memory.h"
#include "cr_cheat_formats.h"
#include "cr_cheats.h"
#include "cr_conflict.h"

#define CHEAT_SEARCH_MAX_DEPTH 6

/* Also the global ptrace-session gate shared with the patch apply/restore paths. */
pthread_mutex_t g_cheat_apply_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct applied_patch {
  intptr_t addr;
  uint8_t *old_bytes;
  size_t len;
} applied_patch_t;
/* Returns 1 if filename appears to match preferred_ver (normalized equality check). */
static int
cheat_ver_matches(const char *name, const char *preferred_ver) {
  if (!preferred_ver || !preferred_ver[0]) return 0;
  if (case_contains(name, preferred_ver)) return 1;
  char norm[32];
  if (cr_version_normalize(preferred_ver, norm, sizeof(norm)) && case_contains(name, norm)) return 1;
  char fver[32];
  if (cr_version_from_filename(name, fver, sizeof(fver)) && cr_version_equal_known(fver, preferred_ver)) return 1;
  return 0;
}

static int
cheat_candidate_score(const cheat_file_search_t *ctx, const char *path, const char *name, int kind) {
  int score = 0;
  if (kind == 1) {
    score += 300;
  } else if (kind == 2) {
    score += 200;
  } else if (kind == 3) {
    score += 100;
  }
  if (case_contains(path, "/cheats/") && case_contains(path, ctx->want)) {
    score += 30;
  }
  if (case_contains(path, "/cheats/json/")) {
    score += 20;
  } else if (case_contains(path, "/cheats/shn/")) {
    score += 10;
  }
  if (ctx->preferred_ver[0] && cheat_ver_matches(name, ctx->preferred_ver)) {
    score += 40;
  }
  return score;
}

static int
cheat_path_component_ok(const char *name) {
  return name && name[0] && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && !strchr(name, '/') && !strchr(name, '\\');
}

static void
find_cheat_file_for_title_dir(const char *dir, int depth, cheat_file_search_t *ctx) {
  if (!dir || !ctx || depth > CHEAT_SEARCH_MAX_DEPTH) {
    return;
  }
  DIR *d = opendir(dir);
  if (!d) {
    return;
  }
  struct dirent *ent = NULL;
  while ((ent = readdir(d)) != NULL) {
    const char *name = ent->d_name;
    if (!cheat_path_component_ok(name)) {
      continue;
    }
    char path[512];
    int pn = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) {
      continue;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      find_cheat_file_for_title_dir(path, depth + 1, ctx);
      continue;
    }
    if (!S_ISREG(st.st_mode)) {
      continue;
    }
    int kind = recognised_cheat_extension(name);
    if (!kind) {
      continue;
    }
    char id[10];
    if (!extract_title_id_prefix(name, id, sizeof(id)) && !find_title_id_in_string(path, id)) {
      continue;
    }
    if (strcmp(id, ctx->want) != 0) {
      continue;
    }
    int score = cheat_candidate_score(ctx, path, name, kind);
    const char *fmt_name = kind == 1 ? "json" : (kind == 2 ? "shn" : "mc4");
    /* store in candidates list (deduplicated) */
    int dup = 0;
    for (int ci = 0; ci < ctx->candidate_count; ci++) {
      if (strcmp(ctx->candidates[ci].path, path) == 0) { dup = 1; break; }
    }
    if (!dup && ctx->candidate_count < MAX_CHEAT_CANDIDATES) {
      snprintf(ctx->candidates[ctx->candidate_count].path, sizeof(ctx->candidates[ctx->candidate_count].path), "%s", path);
      ctx->candidates[ctx->candidate_count].kind = kind;
      ctx->candidates[ctx->candidate_count].score = score;
      ctx->candidate_count++;
    }
    int should_replace = (score > ctx->best_score) ||
                         (score == ctx->best_score && kind < ctx->best_kind);
    if (!should_replace && score == ctx->best_score && kind == ctx->best_kind && ctx->best_path[0]) {
      char cur_vf[32] = "", new_vf[32] = "";
      const char *cur_fname = strrchr(ctx->best_path, '/');
      cur_fname = cur_fname ? cur_fname + 1 : ctx->best_path;
      cr_version_from_filename(cur_fname, cur_vf, sizeof(cur_vf));
      cr_version_from_filename(name, new_vf, sizeof(new_vf));
      if (new_vf[0] && cr_version_compare(new_vf, cur_vf) > 0) {
        char norm_pv[32];
        cr_version_normalize(ctx->preferred_ver, norm_pv, sizeof(norm_pv));
        if (!norm_pv[0] || cr_version_compare(new_vf, norm_pv) <= 0)
          should_replace = 1;
      }
    }
    if (should_replace) {
      if (ctx->log_candidates) {
        cr_log("debug", "cheats", "candidate title=%s rank=%d format=%s path=%s", ctx->want, score, fmt_name, path);
      }
      ctx->best_score = score;
      ctx->best_kind = kind;
      snprintf(ctx->best_path, sizeof(ctx->best_path), "%s", path);
    }
  }
  closedir(d);
}

/* Parse <fmt_name>.txt for saved filenames (bare/relative/full path; URLs skipped). */
static void
scan_local_txt_index(const char *fmt_name, int kind, cheat_file_search_t *ctx) {
  char idx_path[256];
  snprintf(idx_path, sizeof(idx_path), "%s/%s.txt", CHEATRUNNER_CHEATS_DIR, fmt_name);
  char *text = NULL;
  if (read_file_text(idx_path, &text) != 0 || !text) {
    return;
  }
  char *line = text;
  while (line && *line) {
    char *nl = strchr(line, '\n');
    if (nl) { *nl = '\0'; }
    while (*line == ' ' || *line == '\t' || *line == '\r') { line++; }
    if (!*line || *line == '#' ||
        strncmp(line, "http://", 7) == 0 || strncmp(line, "https://", 8) == 0) {
      line = nl ? nl + 1 : NULL;
      continue;
    }
    /* strip inline comment / URL suffix */
    char *eq = strchr(line, '=');
    if (eq) { *eq = '\0'; }
    /* build full path: if no slash, look under fmt subdir and cheats root */
    char full[512];
    struct stat st;
    if (strchr(line, '/') == NULL) {
      snprintf(full, sizeof(full), "%s/%s/%s", CHEATRUNNER_CHEATS_DIR, fmt_name, line);
      if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(full, sizeof(full), "%s/%s", CHEATRUNNER_CHEATS_DIR, line);
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
          line = nl ? nl + 1 : NULL;
          continue;
        }
      }
    } else {
      snprintf(full, sizeof(full), "%s", line);
      if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        line = nl ? nl + 1 : NULL;
        continue;
      }
    }
    const char *fname = strrchr(full, '/');
    fname = fname ? fname + 1 : full;
    if (!is_safe_filename(fname)) {
      line = nl ? nl + 1 : NULL;
      continue;
    }
    char id[10] = {0};
    if (!extract_title_id_prefix(fname, id, sizeof(id)) && !find_title_id_in_string(full, id)) {
      line = nl ? nl + 1 : NULL;
      continue;
    }
    if (strcmp(id, ctx->want) != 0) {
      line = nl ? nl + 1 : NULL;
      continue;
    }
    int score = cheat_candidate_score(ctx, full, fname, kind);
    /* deduplicate */
    int dup = 0;
    for (int ci = 0; ci < ctx->candidate_count; ci++) {
      if (strcmp(ctx->candidates[ci].path, full) == 0) { dup = 1; break; }
    }
    if (!dup && ctx->candidate_count < MAX_CHEAT_CANDIDATES) {
      snprintf(ctx->candidates[ctx->candidate_count].path, sizeof(ctx->candidates[ctx->candidate_count].path), "%s", full);
      ctx->candidates[ctx->candidate_count].kind = kind;
      ctx->candidates[ctx->candidate_count].score = score;
      ctx->candidate_count++;
    }
    int should_replace_ti = (score > ctx->best_score) ||
                             (score == ctx->best_score && kind < ctx->best_kind);
    if (!should_replace_ti && score == ctx->best_score && kind == ctx->best_kind && ctx->best_path[0]) {
      char cur_vf[32] = "", new_vf[32] = "";
      const char *cur_fname = strrchr(ctx->best_path, '/');
      cur_fname = cur_fname ? cur_fname + 1 : ctx->best_path;
      cr_version_from_filename(cur_fname, cur_vf, sizeof(cur_vf));
      cr_version_from_filename(fname, new_vf, sizeof(new_vf));
      if (new_vf[0] && cr_version_compare(new_vf, cur_vf) > 0) {
        char norm_pv[32];
        cr_version_normalize(ctx->preferred_ver, norm_pv, sizeof(norm_pv));
        if (!norm_pv[0] || cr_version_compare(new_vf, norm_pv) <= 0)
          should_replace_ti = 1;
      }
    }
    if (should_replace_ti) {
      if (ctx->log_candidates) {
        cr_log("debug", "cheats", "candidate (txt-idx) title=%s rank=%d format=%s path=%s", ctx->want, score, fmt_name, full);
      }
      ctx->best_score = score;
      ctx->best_kind = kind;
      snprintf(ctx->best_path, sizeof(ctx->best_path), "%s", full);
    }
    line = nl ? nl + 1 : NULL;
  }
  free(text);
}

/* Classify each candidate against ctx->preferred_ver and fill its version field. */
static void
classify_candidates(cheat_file_search_t *ctx) {
  for (int i = 0; i < ctx->candidate_count; i++) {
    const char *fname = strrchr(ctx->candidates[i].path, '/');
    fname = fname ? fname + 1 : ctx->candidates[i].path;
    char fver[32];
    int has_ver = cr_version_from_filename(fname, fver, sizeof(fver));
    if (has_ver) {
      snprintf(ctx->candidates[i].version, sizeof(ctx->candidates[i].version), "%s", fver);
    } else {
      ctx->candidates[i].version[0] = '\0';
    }
    if (!has_ver) {
      ctx->candidates[i].match = CAND_MATCH_GENERIC;
      continue;
    }
    if (!ctx->preferred_ver[0]) {
      ctx->candidates[i].match = CAND_MATCH_UNKNOWN;
      continue;
    }
    ctx->candidates[i].match = cr_version_equal(fver, ctx->preferred_ver)
                                ? CAND_MATCH_EXACT : CAND_MATCH_WRONG_VERSION;
  }
}

/* Pick the best candidate of a given match class; returns 1 if found. */
static int
best_by_match(cheat_file_search_t *ctx, int match_class,
              char *path_out, size_t path_out_sz, int *kind_out, int *score_out) {
  int bk = 99, bs = -1;
  char bp[256] = {0};
  for (int i = 0; i < ctx->candidate_count; i++) {
    if (ctx->candidates[i].match != match_class) continue;
    int ck = ctx->candidates[i].kind, cs = ctx->candidates[i].score;
    if (!bp[0] || ck < bk || (ck == bk && cs > bs)) {
      bk = ck; bs = cs;
      snprintf(bp, sizeof(bp), "%s", ctx->candidates[i].path);
    }
  }
  if (!bp[0]) return 0;
  if (path_out) snprintf(path_out, path_out_sz, "%s", bp);
  if (kind_out) *kind_out = bk;
  if (score_out) *score_out = bs;
  return 1;
}

/* After classification: known version picks exact, then generic, else blocks (wrong version needs force);
 * unknown version keeps the best score from the scan. */
static void
apply_selection_rules(cheat_file_search_t *ctx) {
  ctx->selection_kind = CHEAT_SEL_NONE;

  if (ctx->preferred_ver[0]) {
    /* 1a. Exact match wins unconditionally */
    char ep[256]; int ek = 99, es = -1;
    if (best_by_match(ctx, CAND_MATCH_EXACT, ep, sizeof(ep), &ek, &es)) {
      snprintf(ctx->best_path, sizeof(ctx->best_path), "%s", ep);
      ctx->best_kind  = ek;
      ctx->best_score = es;
      ctx->selection_kind = CHEAT_SEL_EXACT;
      if (ctx->log_candidates) {
        char norm_pv[32];
        cr_version_normalize(ctx->preferred_ver, norm_pv, sizeof(norm_pv));
        const char *efmt = ek == 1 ? "json" : (ek == 2 ? "shn" : "mc4");
        cr_log("debug", "cheat_select",
               "exact_version_match title=%s ver=%s format=%s path=%s",
               ctx->want, norm_pv[0] ? norm_pv : ctx->preferred_ver, efmt, ep);
      }
      return;
    }
    /* 1b. No exact: try generics (no version in filename) */
    char gp[256]; int gk = 99, gs = -1;
    if (best_by_match(ctx, CAND_MATCH_GENERIC, gp, sizeof(gp), &gk, &gs)) {
      snprintf(ctx->best_path, sizeof(ctx->best_path), "%s", gp);
      ctx->best_kind  = gk;
      ctx->best_score = gs;
      ctx->selection_kind = CHEAT_SEL_GENERIC;
      return;
    }
    /* No exact or generic match: fall back to a wrong-version candidate, with a visible warning. */
    char wp[256]; int wk = 99, ws = -1;
    if (best_by_match(ctx, CAND_MATCH_WRONG_VERSION, wp, sizeof(wp), &wk, &ws)) {
      snprintf(ctx->best_path, sizeof(ctx->best_path), "%s", wp);
      ctx->best_kind      = wk;
      ctx->best_score     = ws;
      ctx->selection_kind = CHEAT_SEL_WRONG_VERSION;
      if (ctx->log_candidates) {
        cr_log("debug", "cheat_select",
               "wrong_version_fallback title=%s preferredVer=%s path=%s",
               ctx->want, ctx->preferred_ver, wp);
      }
      return;
    }
    /* Truly no candidates at all */
    ctx->best_path[0] = '\0';
    ctx->best_kind  = 99;
    ctx->best_score = -1;
    return;
  }

  /* 2. Game version unknown: keep score-based best already set by scan */
  if (ctx->best_path[0]) {
    ctx->selection_kind = CHEAT_SEL_GENERIC; /* treat as generic fallback */
  }
}

/* ---- Manual cheat file selection ---- */

#define CHEAT_SELECTIONS_PATH "/data/cheatrunner/cheat_selections.json"
#define MAX_MANUAL_SELECTIONS 64

typedef struct { char title_id[10]; char path[256]; } manual_sel_t;
static manual_sel_t  g_manual_sel[MAX_MANUAL_SELECTIONS];
static int           g_manual_sel_n  = 0;
static int           g_manual_sel_loaded = 0;
static pthread_mutex_t g_manual_sel_lock = PTHREAD_MUTEX_INITIALIZER;

static void manual_sel_load_locked(void) {
  if (g_manual_sel_loaded) return;
  g_manual_sel_loaded = 1;
  g_manual_sel_n = 0;
  char *txt = NULL;
  if (read_file_text(CHEAT_SELECTIONS_PATH, &txt) != 0 || !txt) return;
  cJSON *root = cJSON_Parse(txt);
  free(txt);
  if (!root) return;
  cJSON *item = root->child;
  while (item && g_manual_sel_n < MAX_MANUAL_SELECTIONS) {
    if (cJSON_IsString(item) && item->string && item->valuestring && item->valuestring[0]) {
      char tid[10];
      if (title_id_normalize(item->string, tid)) {
        snprintf(g_manual_sel[g_manual_sel_n].title_id, 10, "%s", tid);
        snprintf(g_manual_sel[g_manual_sel_n].path, 256, "%s", item->valuestring);
        g_manual_sel_n++;
      }
    }
    item = item->next;
  }
  cJSON_Delete(root);
}

static void manual_sel_save_locked(void) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return;
  for (int i = 0; i < g_manual_sel_n; i++) {
    cJSON_AddStringToObject(root, g_manual_sel[i].title_id, g_manual_sel[i].path);
  }
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) return;
  /* Atomic write (temp + rename) so a power loss cannot corrupt the manual-selection file. */
  write_file_atomic(CHEAT_SELECTIONS_PATH, (const uint8_t *)txt, strlen(txt));
  free(txt);
}

int
cheat_manual_get(const char *title_id, char *out, size_t out_sz) {
  char want[10];
  if (!title_id || !out || out_sz < 2) return 0;
  if (!title_id_normalize(title_id, want)) return 0;
  pthread_mutex_lock(&g_manual_sel_lock);
  manual_sel_load_locked();
  for (int i = 0; i < g_manual_sel_n; i++) {
    if (strcmp(g_manual_sel[i].title_id, want) == 0) {
      snprintf(out, out_sz, "%s", g_manual_sel[i].path);
      pthread_mutex_unlock(&g_manual_sel_lock);
      return 1;
    }
  }
  pthread_mutex_unlock(&g_manual_sel_lock);
  return 0;
}

int
cheat_manual_select(const char *title_id, const char *path) {
  char want[10];
  if (!title_id || !path || !path[0]) return -1;
  if (!title_id_normalize(title_id, want)) return -1;
  pthread_mutex_lock(&g_manual_sel_lock);
  manual_sel_load_locked();
  for (int i = 0; i < g_manual_sel_n; i++) {
    if (strcmp(g_manual_sel[i].title_id, want) == 0) {
      snprintf(g_manual_sel[i].path, 256, "%s", path);
      manual_sel_save_locked();
      pthread_mutex_unlock(&g_manual_sel_lock);
      return 0;
    }
  }
  if (g_manual_sel_n < MAX_MANUAL_SELECTIONS) {
    snprintf(g_manual_sel[g_manual_sel_n].title_id, 10, "%s", want);
    snprintf(g_manual_sel[g_manual_sel_n].path, 256, "%s", path);
    g_manual_sel_n++;
    manual_sel_save_locked();
    pthread_mutex_unlock(&g_manual_sel_lock);
    return 0;
  }
  pthread_mutex_unlock(&g_manual_sel_lock);
  return -1;
}

void
cheat_manual_select_clear(const char *title_id) {
  char want[10];
  if (!title_id) return;
  if (!title_id_normalize(title_id, want)) return;
  pthread_mutex_lock(&g_manual_sel_lock);
  manual_sel_load_locked();
  for (int i = 0; i < g_manual_sel_n; i++) {
    if (strcmp(g_manual_sel[i].title_id, want) == 0) {
      g_manual_sel[i] = g_manual_sel[--g_manual_sel_n];
      manual_sel_save_locked();
      break;
    }
  }
  pthread_mutex_unlock(&g_manual_sel_lock);
}

/* Per-title throttle: only log candidates when the title changes. */
static char g_last_cand_title[16] = "";
static pthread_mutex_t g_last_cand_lock = PTHREAD_MUTEX_INITIALIZER;

int
find_cheat_file_for_title(const char *title_id, char *out, size_t out_size, int *kind_out) {
  char want[10];
  if (!title_id_normalize(title_id, want)) return 0;

  /* Check for a manual selection first */
  char manual_path[256];
  if (cheat_manual_get(want, manual_path, sizeof(manual_path))) {
    struct stat mst;
    if (stat(manual_path, &mst) == 0 && S_ISREG(mst.st_mode)) {
      int n = snprintf(out, out_size, "%s", manual_path);
      if (n > 0 && (size_t)n < out_size) {
        if (kind_out) *kind_out = recognised_cheat_extension(manual_path);
        return 1;
      }
    }
    /* File gone — clear stale manual selection */
    cheat_manual_select_clear(want);
  }

  cheat_file_search_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  snprintf(ctx.want_buf, sizeof(ctx.want_buf), "%s", want);
  ctx.want = ctx.want_buf;
  ctx.best_kind = 99;
  ctx.best_score = -1;

  /* Throttle: only log candidates when the title changes. */
  pthread_mutex_lock(&g_last_cand_lock);
  int title_changed = (strcmp(g_last_cand_title, want) != 0);
  if (title_changed) {
    snprintf(g_last_cand_title, sizeof(g_last_cand_title), "%s", want);
  }
  pthread_mutex_unlock(&g_last_cand_lock);
  ctx.log_candidates = title_changed;

  read_param_value_by_title_id(want, "contentVersion", ctx.preferred_ver, sizeof(ctx.preferred_ver));
  if (!ctx.preferred_ver[0]) {
    read_param_value_by_title_id(want, "appVersion", ctx.preferred_ver, sizeof(ctx.preferred_ver));
  }

  find_cheat_file_for_title_dir(CHEATRUNNER_CHEATS_DIR, 0, &ctx);
  find_cheat_file_for_title_dir(ETAHEN_CHEATS_DIR, 0, &ctx);
  find_cheat_file_for_title_dir(ELF_ARSENAL_CHEATS_DIR, 0, &ctx);
  scan_local_txt_index("json", 1, &ctx);
  scan_local_txt_index("shn", 2, &ctx);
  scan_local_txt_index("mc4", 3, &ctx);

  classify_candidates(&ctx);
  apply_selection_rules(&ctx);

  if (!ctx.best_path[0]) return 0;
  const char *sel_fmt = ctx.best_kind == 1 ? "json" : (ctx.best_kind == 2 ? "shn" : "mc4");
  if (ctx.log_candidates) {
    cr_log("info", "cheats", "selected cheat title=%s rank=%d format=%s path=%s ver=%s",
           want, ctx.best_score, sel_fmt,
           ctx.best_path, ctx.preferred_ver[0] ? ctx.preferred_ver : "unknown");
  }
  int n = snprintf(out, out_size, "%s", ctx.best_path);
  if (n <= 0 || (size_t)n >= out_size) return 0;
  if (kind_out) *kind_out = ctx.best_kind;
  return 1;
}

/* Resolves a saved mod name to its index in the selected cheat file. Keyed by name because
 * indices shift when the file is updated; returns -1 if not found. */
int
cheat_find_mod_index_by_name(const char *title_id, const char *mod_name) {
  if (!mod_name || !mod_name[0]) return -1;
  char path[256];
  int kind = 0;
  if (!find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) return -1;

  char *txt = NULL;
  if (read_file_text(path, &txt) != 0 || !txt) return -1;

  char *json_txt = NULL;
  if (kind == 1) {
    json_txt = txt;
  } else if (kind == 2) {
    json_txt = shn_xml_to_json(txt, strlen(txt));
    free(txt);
  } else if (kind == 3) {
    char *xml = mc4_decrypt_to_xml(txt, strlen(txt), NULL);
    free(txt);
    if (!xml) return -1;
    json_txt = shn_xml_to_json(xml, strlen(xml));
    free(xml);
  } else {
    free(txt);
  }
  if (!json_txt) return -1;

  cJSON *root = cJSON_Parse(json_txt);
  free(json_txt);
  if (!root) return -1;

  int found = -1;
  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (cJSON_IsArray(mods)) {
    int idx = 0;
    cJSON *m = NULL;
    cJSON_ArrayForEach(m, mods) {
      cJSON *name_j = cJSON_GetObjectItem(m, "name");
      if (cJSON_IsString(name_j) && name_j->valuestring && !strcmp(name_j->valuestring, mod_name)) {
        found = idx;
        break;
      }
      idx++;
    }
  }
  cJSON_Delete(root);
  return found;
}

/* Fills ctx_out with all candidates and the best one. A non-empty override_ver (e.g. the live
 * game version) wins over the SFO lookup. */
int
find_cheat_candidates_ex(const char *title_id, const char *override_ver, cheat_file_search_t *ctx_out) {
  char want[10];
  if (!title_id_normalize(title_id, want)) return 0;
  cheat_file_search_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  snprintf(ctx.want_buf, sizeof(ctx.want_buf), "%s", want);
  ctx.want       = ctx.want_buf;
  ctx.best_kind  = 99;
  ctx.best_score = -1;
  ctx.log_candidates = 0;

  if (override_ver && override_ver[0]) {
    snprintf(ctx.preferred_ver, sizeof(ctx.preferred_ver), "%s", override_ver);
  } else {
    read_param_value_by_title_id(want, "contentVersion", ctx.preferred_ver, sizeof(ctx.preferred_ver));
    if (!ctx.preferred_ver[0])
      read_param_value_by_title_id(want, "appVersion", ctx.preferred_ver, sizeof(ctx.preferred_ver));
  }

  find_cheat_file_for_title_dir(CHEATRUNNER_CHEATS_DIR, 0, &ctx);
  find_cheat_file_for_title_dir(ETAHEN_CHEATS_DIR, 0, &ctx);
  find_cheat_file_for_title_dir(ELF_ARSENAL_CHEATS_DIR, 0, &ctx);
  scan_local_txt_index("json", 1, &ctx);
  scan_local_txt_index("shn", 2, &ctx);
  scan_local_txt_index("mc4", 3, &ctx);

  classify_candidates(&ctx);
  apply_selection_rules(&ctx);

  /* Check for a manual selection and mark it in candidates */
  char manual_path[256];
  if (cheat_manual_get(want, manual_path, sizeof(manual_path))) {
    /* Find matching candidate and override selection */
    for (int i = 0; i < ctx.candidate_count; i++) {
      if (strcmp(ctx.candidates[i].path, manual_path) == 0) {
        snprintf(ctx.best_path, sizeof(ctx.best_path), "%s", manual_path);
        ctx.best_kind  = ctx.candidates[i].kind;
        ctx.best_score = ctx.candidates[i].score;
        ctx.selection_kind = CHEAT_SEL_MANUAL;
        break;
      }
    }
  }

  if (ctx_out) *ctx_out = ctx;
  return ctx.best_path[0] ? 1 : 0;
}

int
find_cheat_candidates(const char *title_id, cheat_file_search_t *ctx_out) {
  return find_cheat_candidates_ex(title_id, NULL, ctx_out);
}


/* Live re-check: verify the expected app is still running and hasn't changed pid/appId. */
static int
cr_check_app_safe(const char *title_id, pid_t expected_pid, int expected_app_id,
                  char *err, size_t err_size) {
  int live_id = sceSystemServiceGetAppIdOfRunningBigApp();
  if (live_id <= 0) {
    snprintf(err, err_size, "game is no longer running (BigApp gone)");
    cr_log("warn", "cheats.guard",
           "unsafe app state title=%s expected_appId=0x%x live_appId=none state=not_running",
           title_id, expected_app_id);
    return -1;
  }
  if (live_id != expected_app_id) {
    snprintf(err, err_size, "game changed (was appId=0x%x, now 0x%x)", expected_app_id, live_id);
    cr_log("warn", "cheats.guard",
           "unsafe app state title=%s expected_appId=0x%x live_appId=0x%x state=swapped",
           title_id, expected_app_id, live_id);
    return -1;
  }
  pid_t live_pid = find_pid_for_app_id((uint32_t)live_id);
  if (live_pid != expected_pid) {
    snprintf(err, err_size, "game pid changed (expected %d, got %d)", (int)expected_pid, (int)live_pid);
    cr_log("warn", "cheats.guard",
           "unsafe app state title=%s appId=0x%x expected_pid=%d live_pid=%d state=pid_changed",
           title_id, expected_app_id, (int)expected_pid, (int)live_pid);
    return -1;
  }
  return 0;
}


mod_disabled_rec_t g_mods_disabled[MOD_DISABLED_MAX];
int g_mods_disabled_n = 0;
pthread_mutex_t g_mods_disabled_lock = PTHREAD_MUTEX_INITIALIZER;
static void mod_disabled_set(const char *tid, pid_t pid, int mod) {
  pthread_mutex_lock(&g_mods_disabled_lock);
  for (int i = 0; i < g_mods_disabled_n; i++) {
    if (g_mods_disabled[i].mod_index == mod && strcmp(g_mods_disabled[i].title_id, tid) == 0) {
      g_mods_disabled[i].pid = pid;
      pthread_mutex_unlock(&g_mods_disabled_lock); return;
    }
  }
  if (g_mods_disabled_n < MOD_DISABLED_MAX) {
    snprintf(g_mods_disabled[g_mods_disabled_n].title_id, sizeof(g_mods_disabled[0].title_id), "%s", tid);
    g_mods_disabled[g_mods_disabled_n].pid = pid;
    g_mods_disabled[g_mods_disabled_n].mod_index = mod;
    g_mods_disabled_n++;
  }
  pthread_mutex_unlock(&g_mods_disabled_lock);
}
static void mod_disabled_clear(const char *tid, int mod) {
  pthread_mutex_lock(&g_mods_disabled_lock);
  for (int i = 0; i < g_mods_disabled_n; i++) {
    if (g_mods_disabled[i].mod_index == mod && strcmp(g_mods_disabled[i].title_id, tid) == 0) {
      g_mods_disabled[i] = g_mods_disabled[--g_mods_disabled_n]; break;
    }
  }
  pthread_mutex_unlock(&g_mods_disabled_lock);
}
int mod_disabled_check(const char *tid, pid_t pid, int mod) {
  pthread_mutex_lock(&g_mods_disabled_lock);
  for (int i = 0; i < g_mods_disabled_n; i++) {
    if (g_mods_disabled[i].mod_index == mod && g_mods_disabled[i].pid == pid &&
        strcmp(g_mods_disabled[i].title_id, tid) == 0) {
      pthread_mutex_unlock(&g_mods_disabled_lock); return 1;
    }
  }
  pthread_mutex_unlock(&g_mods_disabled_lock); return 0;
}

/* Tracks mods explicitly enabled this session so BASELINE_UNKNOWN can show as ON_UNVERIFIED. */
static mod_disabled_rec_t g_mods_enabled_arr[MOD_DISABLED_MAX];
static int g_mods_enabled_n = 0;
static pthread_mutex_t g_mods_enabled_lock = PTHREAD_MUTEX_INITIALIZER;
static void mod_enabled_set(const char *tid, pid_t pid, int mod) {
  pthread_mutex_lock(&g_mods_enabled_lock);
  for (int i = 0; i < g_mods_enabled_n; i++) {
    if (g_mods_enabled_arr[i].mod_index == mod && strcmp(g_mods_enabled_arr[i].title_id, tid) == 0) {
      g_mods_enabled_arr[i].pid = pid;
      pthread_mutex_unlock(&g_mods_enabled_lock); return;
    }
  }
  if (g_mods_enabled_n < MOD_DISABLED_MAX) {
    snprintf(g_mods_enabled_arr[g_mods_enabled_n].title_id, sizeof(g_mods_enabled_arr[0].title_id), "%s", tid);
    g_mods_enabled_arr[g_mods_enabled_n].pid = pid;
    g_mods_enabled_arr[g_mods_enabled_n].mod_index = mod;
    g_mods_enabled_n++;
  }
  pthread_mutex_unlock(&g_mods_enabled_lock);
}
static void mod_enabled_clear(const char *tid, int mod) {
  pthread_mutex_lock(&g_mods_enabled_lock);
  for (int i = 0; i < g_mods_enabled_n; i++) {
    if (g_mods_enabled_arr[i].mod_index == mod && strcmp(g_mods_enabled_arr[i].title_id, tid) == 0) {
      g_mods_enabled_arr[i] = g_mods_enabled_arr[--g_mods_enabled_n]; break;
    }
  }
  pthread_mutex_unlock(&g_mods_enabled_lock);
}
int mod_enabled_check(const char *tid, pid_t pid, int mod) {
  pthread_mutex_lock(&g_mods_enabled_lock);
  for (int i = 0; i < g_mods_enabled_n; i++) {
    if (g_mods_enabled_arr[i].mod_index == mod && g_mods_enabled_arr[i].pid == pid &&
        strcmp(g_mods_enabled_arr[i].title_id, tid) == 0) {
      pthread_mutex_unlock(&g_mods_enabled_lock); return 1;
    }
  }
  pthread_mutex_unlock(&g_mods_enabled_lock); return 0;
}
void mod_enabled_clear_for_pid(pid_t pid) {
  pthread_mutex_lock(&g_mods_enabled_lock);
  for (int i = g_mods_enabled_n - 1; i >= 0; i--) {
    if (g_mods_enabled_arr[i].pid == pid) {
      g_mods_enabled_arr[i] = g_mods_enabled_arr[--g_mods_enabled_n];
    }
  }
  pthread_mutex_unlock(&g_mods_enabled_lock);
}
int cheat_any_enabled_for_title(const char *title_id, pid_t pid) {
  pthread_mutex_lock(&g_mods_enabled_lock);
  for (int i = 0; i < g_mods_enabled_n; i++) {
    if (g_mods_enabled_arr[i].pid == pid &&
        strcmp(g_mods_enabled_arr[i].title_id, title_id) == 0) {
      pthread_mutex_unlock(&g_mods_enabled_lock);
      return 1;
    }
  }
  pthread_mutex_unlock(&g_mods_enabled_lock);
  return 0;
}

crash_guard_state_t g_crash_guard_arr[CRASH_GUARD_MAX];
int g_crash_guard_n = 0;
crash_suspect_rec_t g_crash_suspects[CRASH_SUSPECT_MAX];
int g_crash_suspects_n = 0;
pthread_mutex_t g_crash_guard_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t g_last_game_exit_lock = PTHREAD_MUTEX_INITIALIZER;
last_game_exit_t g_last_game_exit = {0};
pthread_mutex_t g_last_apply_lock = PTHREAD_MUTEX_INITIALIZER;
last_apply_rec_t g_last_apply_rec = {0};
_Atomic int g_cheat_applying = 0;
volatile uint64_t g_last_apply_at_ms = 0;
volatile uint64_t g_post_apply_guard_until_ms = 0;

void
crash_suspects_save(void) {
  pthread_mutex_lock(&g_crash_guard_lock);
  cJSON *root = cJSON_CreateArray();
  if (root) {
    for (int i = 0; i < g_crash_suspects_n; i++) {
      cJSON *e = cJSON_CreateObject();
      if (!e) continue;
      cJSON_AddStringToObject(e, "titleId",   g_crash_suspects[i].title_id);
      cJSON_AddNumberToObject(e, "modIndex",  g_crash_suspects[i].mod_index);
      cJSON_AddStringToObject(e, "modName",   g_crash_suspects[i].mod_name);
      cJSON_AddNumberToObject(e, "elapsed_ms",(double)g_crash_suspects[i].elapsed_ms);
      cJSON_AddNumberToObject(e, "ts",        (double)(uint64_t)g_crash_suspects[i].ts);
      cJSON_AddItemToArray(root, e);
    }
  }
  pthread_mutex_unlock(&g_crash_guard_lock);
  if (!root) return;
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!txt) return;
  write_file_atomic(CHEATRUNNER_CRASH_SUSPECTS_PATH, (const uint8_t *)txt, strlen(txt));
  free(txt);
}

void
crash_suspects_load(void) {
  char *txt = NULL;
  if (read_file_text(CHEATRUNNER_CRASH_SUSPECTS_PATH, &txt) != 0 || !txt) return;
  cJSON *root = cJSON_Parse(txt);
  free(txt);
  if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return; }
  pthread_mutex_lock(&g_crash_guard_lock);
  g_crash_suspects_n = 0;
  time_t now_t = time(NULL);
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    if (g_crash_suspects_n >= CRASH_SUSPECT_MAX) break;
    cJSON *tid_j = cJSON_GetObjectItem(item, "titleId");
    cJSON *mi_j  = cJSON_GetObjectItem(item, "modIndex");
    cJSON *mn_j  = cJSON_GetObjectItem(item, "modName");
    cJSON *el_j  = cJSON_GetObjectItem(item, "elapsed_ms");
    cJSON *ts_j  = cJSON_GetObjectItem(item, "ts");
    if (!cJSON_IsString(tid_j) || !tid_j->valuestring || !tid_j->valuestring[0]) continue;
    if (!cJSON_IsNumber(mi_j)) continue;
    crash_suspect_rec_t *r = &g_crash_suspects[g_crash_suspects_n];
    memset(r, 0, sizeof(*r));
    snprintf(r->title_id, sizeof(r->title_id), "%s", tid_j->valuestring);
    r->mod_index = (int)mi_j->valuedouble;
    if (cJSON_IsString(mn_j) && mn_j->valuestring)
      snprintf(r->mod_name, sizeof(r->mod_name), "%s", mn_j->valuestring);
    if (cJSON_IsNumber(el_j)) r->elapsed_ms = (uint64_t)el_j->valuedouble;
    if (cJSON_IsNumber(ts_j)) r->ts = (time_t)ts_j->valuedouble;
    r->pid = -1;
    r->app_id = 0;
    /* Discard suspects older than 7 days — stale after CR restarts or config fixes. */
    if (r->ts > 0 && now_t > r->ts && (now_t - r->ts) > 7 * 24 * 3600) continue;
    g_crash_suspects_n++;
  }
  int loaded = g_crash_suspects_n;
  pthread_mutex_unlock(&g_crash_guard_lock);
  cJSON_Delete(root);
  if (loaded > 0)
    cr_log("info", "cheats.guard", "loaded %d crash suspect(s) from disk", loaded);
}

/* True when a mod has a cave-sized (>=16 byte) ON value. Real master codes define a shared cave;
 * dependents that only mention "master code" in their name do not. */
static int
mod_has_cave_entry(cJSON *mod) {
  cJSON *mem = cJSON_GetObjectItem(mod, "memory");
  if (!cJSON_IsArray(mem)) return 0;
  cJSON *e = NULL;
  cJSON_ArrayForEach(e, mem) {
    cJSON *on_j = cJSON_GetObjectItem(e, "on");
    if (!cJSON_IsString(on_j) || !on_j->valuestring) continue;
    size_t need = hex_bytes_need(on_j->valuestring);
    uint8_t *buf CR_AUTOFREE = malloc(need);
    size_t len = 0;
    if (buf && parse_hex_bytes_checked(on_j->valuestring, buf, need, &len) == 0 && len >= 16) {
      return 1;
    }
  }
  return 0;
}

/* Nearest preceding mod named "mastercode" that defines a cave; it governs target_mod_idx.
 * The cave check skips dependents that only mention master code in their name. */
cJSON *
find_master_code_mod_for(cJSON *mods, int target_mod_idx) {
  cJSON *m = NULL;
  cJSON *last_mc = NULL;
  int i = 0;
  cJSON_ArrayForEach(m, mods) {
    if (i == target_mod_idx) return last_mc;
    cJSON *name_j = cJSON_GetObjectItem(m, "name");
    if (cJSON_IsString(name_j) && name_j->valuestring &&
        (strcasestr(name_j->valuestring, "mastercode") ||
         strcasestr(name_j->valuestring, "master code")) &&
        mod_has_cave_entry(m)) {
      last_mc = m;
    }
    i++;
  }
  return last_mc;
}

/* Returns 1 if the mod name indicates an MC-dependent cheat (contains "MC" but not "master code"). */
static int
mod_is_mc_dependent(const char *name) {
  if (!name) return 0;
  if (strcasestr(name, "master code") || strcasestr(name, "mastercode")) return 0;
  return strstr(name, "MC") != NULL;
}

/* Fallback fixup: high bytes of the master code offset plus the low byte of the dependent offset. */
static uint64_t
fixup_mc_dependent_addr(uint64_t mc_base_off, uint64_t dep_off) {
  return (mc_base_off & ~(uint64_t)0xff) | (dep_off & 0xff);
}

/* Scans the live MC region for the dep_off bytes, else uses the low-byte formula.
 * Returns 0 on read failure (addr_out unchanged). */
int
mc_scan_dep_addr(pid_t pid, intptr_t mc_addr,
                 const uint8_t *mc_on, size_t mc_on_len,
                 const uint8_t *dep_off, size_t dep_off_len,
                 uint64_t mc_base_off, uint64_t dep_raw_off,
                 intptr_t *addr_out) {
  (void)mc_on;
  if (!mc_on_len || !dep_off_len || dep_off_len > mc_on_len || !addr_out) return 0;
  /* Identical bytes (NOP padding, zero-fill) would match the first run in the MC payload,
   * so use the low-byte formula directly. */
  {
    int uniform = 1;
    for (size_t _u = 1; _u < dep_off_len; _u++) {
      if (dep_off[_u] != dep_off[0]) { uniform = 0; break; }
    }
    if (uniform) {
      *addr_out = (mc_addr - (intptr_t)mc_base_off) +
                  (intptr_t)((mc_base_off & ~(uint64_t)0xff) | (dep_raw_off & 0xff));
      return 1;
    }
  }
  uint8_t *buf = malloc(mc_on_len);
  if (!buf) return 0;
  if (read_process_memory(pid, mc_addr, buf, mc_on_len) != 0) {
    free(buf);
    return 0;  /* can't read MC region — keep caller's resolved addr intact */
  }
  for (size_t i = 0; i + dep_off_len <= mc_on_len; i++) {
    if (memcmp(buf + i, dep_off, dep_off_len) == 0) {
      *addr_out = mc_addr + (intptr_t)i;
      free(buf);
      return 1;
    }
  }
  free(buf);
  /* mod_base = mc_addr - mc_base_off; build the absolute address the way cheat_resolve_addr does. */
  *addr_out = (mc_addr - (intptr_t)mc_base_off) +
              (intptr_t)((mc_base_off & ~(uint64_t)0xff) | (dep_raw_off & 0xff));
  return 1;
}

/* Returns the first entry's offset from a mod's memory array, or 0 if unavailable. */
uint64_t
mc_mod_first_offset(cJSON *mc_mod) {
  cJSON *mem = cJSON_GetObjectItem(mc_mod, "memory");
  if (!cJSON_IsArray(mem) || cJSON_GetArraySize(mem) == 0) return 0;
  cJSON *first = cJSON_GetArrayItem(mem, 0);
  cJSON *off_j = cJSON_GetObjectItem(first, "offset");
  if (!cJSON_IsString(off_j)) return 0;
  uint64_t off = 0;
  parse_offset_hex_checked(off_j->valuestring, &off);
  return off;
}

void
fmt_hex16(const uint8_t *b, size_t len, char *buf, size_t buf_sz) {
  size_t n = len < 16 ? len : 16;
  size_t pos = 0;
  for (size_t i = 0; i < n && pos + 4 < buf_sz; i++) {
    pos += (size_t)snprintf(buf + pos, buf_sz - pos, "%02x ", b[i]);
  }
  if (len > 16 && pos + 4 < buf_sz) {
    snprintf(buf + pos, buf_sz - pos, "...");
  }
}

static int
apply_cheat_json_impl(const char *title_id, int mod_index, int turn_on, char *err, size_t err_size,
                       applied_patch_t *applied, uint8_t **wi_data) {
  char path[256];
  int kind = 0;
  pid_t pid = -1;
  intptr_t base = 0;
  int app_id_live = 0;
  char running_title[16] = {0};
  char running_name[256] = {0};
  char run_norm[10] = {0};
  char cheat_norm[10] = {0};
  int rc = -1;
  int pre_n = 0;                /* entries pre-read */
  int      wi_is_cave[64];      /* 1 if len >= 16 (code cave) */
  int      written_order[64];   /* pre_n indices in actual write order (for rollback) */
  int      write_ok_n = 0;      /* entries successfully written */

  if (!find_cheat_file_for_title(title_id, path, sizeof(path), &kind)) {
    snprintf(err, err_size, "no cheat file for %s", title_id);
    return -1;
  }
  cr_log("info", "cheats", "apply request title=%s mod=%d on=%d file=%s format=%s", title_id, mod_index, turn_on ? 1 : 0,
         path, kind == 1 ? "json" : (kind == 2 ? "shn" : "mc4"));
  pthread_mutex_lock(&g_cfg_lock);
  int engine_enabled    = g_cfg.cheat_engine;
  int restore_rx        = g_cfg.cheat_restore_rx;
  int restore_orig_prot = g_cfg.cheat_restore_original_prot;
  int one_at_a_time     = g_cfg.cheat_apply_one_at_a_time;
  int cooldown_ms       = g_cfg.cheat_apply_cooldown_ms;
  int min_stable_ms     = g_cfg.cheat_min_stable_ms;
  int mc_fixup          = g_cfg.cheat_master_code_fixup;
  int inter_mod_delay_ms = g_cfg.cheat_inter_mod_delay_ms;
  pthread_mutex_unlock(&g_cfg_lock);
  if (!engine_enabled) {
    snprintf(err, err_size, "cheat engine disabled");
    return -1;
  }
  if (get_running_game_ex(&pid, running_title, sizeof(running_title), &base, &app_id_live) != 0) {
    snprintf(err, err_size, "no game is currently running");
    return -1;
  }
  cr_log("info", "cheats", "running title=%s pid=%d base=0x%lx", running_title, (int)pid, (long)base);
  running_game_state_t run_state;
  if (read_running_state(&run_state) == 0) {
    snprintf(running_name, sizeof(running_name), "%s", run_state.title_name);
  }
  if (!running_name[0]) {
    read_param_value_by_title_id(running_title, "titleName", running_name, sizeof(running_name));
  }
  if (!running_name[0]) {
    snprintf(running_name, sizeof(running_name), "%s", running_title);
  }
  /* read_running_state() never populates started_at; inherit from the game monitor's cached state. */
  if (run_state.started_at == 0) {
    running_game_state_t cached_st;
    running_state_get(&cached_st);
    if (cached_st.running && cached_st.started_at > 0 &&
        strcmp(cached_st.title_id, running_title) == 0) {
      run_state.started_at = cached_st.started_at;
    }
  }
  if (!title_id_normalize(running_title, run_norm) || !title_id_normalize(title_id, cheat_norm) ||
      strcmp(run_norm, cheat_norm) != 0) {
    snprintf(err, err_size, "running game (%s) does not match cheat target (%s)", running_title, title_id);
    return -1;
  }

  /* The game must have run for cheat_min_stable_ms; started_at == 0 means just detected, so uptime is 0. */
  if (min_stable_ms > 0) {
    uint64_t uptime_ms = 0;
    if (run_state.started_at > 0) {
      uint64_t now_sec = (uint64_t)time(NULL);
      if (now_sec > run_state.started_at)
        uptime_ms = (now_sec - run_state.started_at) * 1000;
    }
    if (uptime_ms < (uint64_t)min_stable_ms) {
      snprintf(err, err_size, "app_not_stable");
      cr_log("warn", "cheats.guard",
             "app not stable title=%s appId=0x%x pid=%d uptimeMs=%llu min_stable_ms=%d",
             title_id, app_id_live, (int)pid,
             (unsigned long long)uptime_ms, min_stable_ms);
      return -1;
    }
  }

  /* base==0 means the module hasn't mapped yet — relative addrs would equal the raw offset. */
  if (base == 0 && kind != 1) {
    snprintf(err, err_size, "base_not_ready");
    cr_log("warn", "cheats.guard",
           "base not ready title=%s pid=%d kind=%d — refusing apply until module maps",
           title_id, (int)pid, kind);
    return -1;
  }

  char *txt = NULL;
  if (read_file_text(path, &txt) != 0 || !txt) {
    snprintf(err, err_size, "could not read cheat file");
    return -1;
  }

  char *json_txt = NULL;
  if (kind == 1) {
    json_txt = txt;
  } else if (kind == 2) {
    json_txt = shn_xml_to_json(txt, strlen(txt));
    free(txt);
  } else if (kind == 3) {
    char *xml = mc4_decrypt_to_xml(txt, strlen(txt), NULL);
    free(txt);
    if (!xml) {
      snprintf(err, err_size, "MC4 decrypt failed");
      return -1;
    }
    json_txt = shn_xml_to_json(xml, strlen(xml));
    free(xml);
  }
  if (!json_txt) {
    snprintf(err, err_size, "cheat format parse failed");
    return -1;
  }

  cJSON *root = cJSON_Parse(json_txt);
  free(json_txt);
  if (!root) {
    snprintf(err, err_size, "cheat JSON parse failed");
    return -1;
  }

  cJSON *mods = cJSON_GetObjectItem(root, "mods");
  if (!cJSON_IsArray(mods) || mod_index < 0 || mod_index >= cJSON_GetArraySize(mods)) {
    cJSON_Delete(root);
    snprintf(err, err_size, "cheat index out of range");
    return -1;
  }
  cJSON *mod = cJSON_GetArrayItem(mods, mod_index);
  cJSON *mname_j = cJSON_GetObjectItem(mod, "module_name");
  const char *module_name = (cJSON_IsString(mname_j) && mname_j->valuestring) ? mname_j->valuestring : "";
  cJSON *memory = cJSON_GetObjectItem(mod, "memory");
  if (!cJSON_IsArray(memory)) {
    memory = cJSON_GetObjectItem(mod, "patches");
  }
  cJSON *type_j = cJSON_GetObjectItem(mod, "type");
  const char *type = (cJSON_IsString(type_j) && type_j->valuestring) ? type_j->valuestring : "checkbox";
  /* Button cheats always fire ON, unless the UI sends on=0 to undo them. */
  int effective_on = (!strcasecmp(type, "button") && turn_on) ? 1 : (turn_on ? 1 : 0);
  if (!cJSON_IsArray(memory) || cJSON_GetArraySize(memory) == 0) {
    cJSON_Delete(root);
    snprintf(err, err_size, "mod has no memory entries");
    return -1;
  }

  if (!cr_priv_can_patch_game_memory()) {
    cJSON_Delete(root);
    snprintf(err, err_size,
             "privilege bootstrap incomplete; cannot patch game memory (check /api/dev/privileges)");
    cr_log("error", "cheats", "apply blocked: canPatchGameMemory=false title=%s mod=%d", title_id, mod_index);
    return -1;
  }

  /* Check the cooldown before auto-disabling conflicts, or both cheats end up OFF. */
  if (cooldown_ms > 0 && g_last_apply_at_ms > 0) {
    uint64_t _pre_elapsed = now_ms() - g_last_apply_at_ms;
    if (_pre_elapsed < (uint64_t)cooldown_ms) {
      cJSON_Delete(root);
      snprintf(err, err_size, "Cooldown active: wait %llums before next toggle.",
               (unsigned long long)((uint64_t)cooldown_ms - _pre_elapsed));
      return -2;
    }
  }

  /* Auto-disable conflicts before taking the apply lock; the recursive apply calls take it themselves. */
  if (effective_on) {
    char *conflict_json = cJSON_PrintUnformatted(root);
    if (conflict_json) {
      cheat_conflict_map_t cmap;
      if (conflict_map_build(conflict_json, &cmap) == 0) {
        int conflict_mods[16];
        int cn = conflict_map_get_for_mod(&cmap, mod_index, conflict_mods, 16);
        for (int _ci = 0; _ci < cn; _ci++) {
          int cm = conflict_mods[_ci];
          if (mod_enabled_check(title_id, pid, cm)) {
            cr_log("info", "cheats.conflict",
                   "auto_disable conflicting mod=%d (%s) before enabling mod=%d (%s) title=%s",
                   cm, conflict_map_mod_name(&cmap, cm),
                   mod_index, conflict_map_mod_name(&cmap, mod_index), title_id);
            char _cerr[128] = {0};
            int _crc = apply_cheat_json(title_id, cm, 0, _cerr, sizeof(_cerr));
            if (_crc != 0) {
              cr_log("warn", "cheats.conflict",
                     "auto_disable mod=%d failed rc=%d err=%s", cm, _crc, _cerr);
            }
          }
        }
      }
      free(conflict_json);
    }
  }

  /* Auto-enable the file mastercode first, also before the lock for the same reason. */
  if (effective_on) {
    cJSON *mc_m = find_master_code_mod_for(mods, mod_index);
    if (mc_m && mc_m != mod) {
      int mc_idx = -1;
      { cJSON *_it = NULL; int _ii = 0;
        cJSON_ArrayForEach(_it, mods) { if (_it == mc_m) { mc_idx = _ii; break; } _ii++; } }
      if (mc_idx >= 0 && !mod_enabled_check(title_id, pid, mc_idx)) {
        cJSON *mc_name_j = cJSON_GetObjectItem(mc_m, "name");
        const char *mc_name = (cJSON_IsString(mc_name_j) && mc_name_j->valuestring)
                              ? mc_name_j->valuestring : "Mastercode";
        cr_log("info", "cheats", "mastercode_auto_enable title=%s mc_idx=%d name=\"%s\" before_mod=%d",
               title_id, mc_idx, mc_name, mod_index);
        char mc_err[128] = {0};
        g_last_apply_at_ms = 0;
        int mc_rc = apply_cheat_json(title_id, mc_idx, 1, mc_err, sizeof(mc_err));
        if (mc_rc != 0) {
          cr_log("warn", "cheats", "mastercode_auto_enable failed mc_idx=%d rc=%d err=%s",
                 mc_idx, mc_rc, mc_err);
        }
      }
    }
  }

  if (one_at_a_time) {
    if (pthread_mutex_trylock(&g_cheat_apply_lock) != 0) {
      cJSON_Delete(root);
      snprintf(err, err_size,
               "Another cheat is being applied. Wait for the safety window to finish.");
      cr_log("warn", "cheats.guard", "apply_in_progress rejected title=%s mod=%d", title_id, mod_index);
      return -2;
    }
  } else {
    pthread_mutex_lock(&g_cheat_apply_lock);
  }

  /* Cooldown check */
  if (cooldown_ms > 0 && g_last_apply_at_ms > 0) {
    uint64_t _elapsed_cd = now_ms() - g_last_apply_at_ms;
    if (_elapsed_cd < (uint64_t)cooldown_ms) {
      pthread_mutex_unlock(&g_cheat_apply_lock);
      cJSON_Delete(root);
      snprintf(err, err_size, "Cooldown active: wait %llums before next toggle.",
               (unsigned long long)((uint64_t)cooldown_ms - _elapsed_cd));
      cr_log("warn", "cheats.guard",
             "cooldown active title=%s mod=%d remaining_ms=%llu",
             title_id, mod_index,
             (unsigned long long)((uint64_t)cooldown_ms - _elapsed_cd));
      return -2;
    }
  }

  /* The game can exit after the initial check; revalidate before ptrace. */
  {
    char _ge[128] = {0};
    if (cr_check_app_safe(title_id, pid, app_id_live, _ge, sizeof(_ge)) != 0) {
      pthread_mutex_unlock(&g_cheat_apply_lock);
      cJSON_Delete(root);
      snprintf(err, err_size, "Game is not in a safe running state: %s", _ge);
      return -1;
    }
  }

  /* Suppress scanner refresh during the write loop. */
  g_cheat_applying = 1;
  cr_log("info", "cheats.guard",
         "pausing scanners during apply title=%s mod=%d pid=%d appId=0x%x",
         title_id, mod_index, (int)pid, app_id_live);

  /* mdbg I/O needs no ptrace attach; just confirm the process is still alive. */
  if (kill(pid, 0) != 0 && errno == ESRCH) {
    g_cheat_applying = 0;
    pthread_mutex_unlock(&g_cheat_apply_lock);
    cJSON_Delete(root);
    pid_t live_pid = (app_id_live > 0) ? find_pid_for_app_id((uint32_t)app_id_live) : -1;
    if (live_pid <= 0) {
      snprintf(err, err_size, "game has exited");
      rpc_refresh_title_and_notify();
    } else if (live_pid != pid) {
      snprintf(err, err_size, "game restarted (pid changed)");
      rpc_refresh_title_and_notify();
    } else {
      snprintf(err, err_size, "game process unresponsive");
    }
    return -1;
  }

  /* Resolve per-mod base (may differ from eboot base if module_name is set) */
  intptr_t mod_base = base;
  if (module_name[0]) {
    if (resolve_module_base(pid, module_name, base, &mod_base) != 0) {
      g_cheat_applying = 0;
      pthread_mutex_unlock(&g_cheat_apply_lock);
      cJSON_Delete(root);
      snprintf(err, err_size, "module_not_loaded: '%s' is not loaded in pid=%d", module_name, (int)pid);
      return -1;
    }
    cr_log("info", "cheats", "module_base '%s'=0x%lx title=%s mod=%d",
           module_name, (long)mod_base, title_id, mod_index);
  }

  /* PS2 emulation detection — all addresses are absolute when PS2 emu is running */
  int is_ps2 = process_is_ps2_emu(pid);
  if (is_ps2) {
    cr_log("info", "cheats", "ps2_emu detected pid=%d all addrs treated as absolute", (int)pid);
  }

  /* Master Code fixup setup (only when enabled in config) */
  int do_mc_fixup = 0;
  int mc_fallback_available = 0;
  uint64_t mc_base_off = 0;
  uint8_t *mc_on_bytes_buf CR_AUTOFREE = NULL;
  size_t mc_on_len = 0;
  intptr_t mc_live_addr = 0;
  if (mc_fixup) {
    cJSON *mc_mod = find_master_code_mod_for(mods, mod_index);
    if (mc_mod) {
      mc_base_off = mc_mod_first_offset(mc_mod);
      /* Also parse MC ON bytes for runtime scan */
      cJSON *mc_mem0 = cJSON_GetObjectItem(mc_mod, "memory");
      cJSON *mc_e0 = (cJSON_IsArray(mc_mem0) && cJSON_GetArraySize(mc_mem0) > 0)
                     ? cJSON_GetArrayItem(mc_mem0, 0) : NULL;
      if (mc_e0) {
        cJSON *mc_on_j = cJSON_GetObjectItem(mc_e0, "on");
        if (cJSON_IsString(mc_on_j) && mc_on_j->valuestring) {
          size_t mc_need = hex_bytes_need(mc_on_j->valuestring);
          mc_on_bytes_buf = malloc(mc_need);
          if (mc_on_bytes_buf &&
              parse_hex_bytes_checked(mc_on_j->valuestring, mc_on_bytes_buf, mc_need, &mc_on_len) != 0)
            mc_on_len = 0;
        }
      }
      if (mc_base_off != 0) {
        mc_live_addr = mod_base + (intptr_t)mc_base_off;
        mc_fallback_available = 1;
        cJSON *name_j_mc = cJSON_GetObjectItem(mod, "name");
        const char *mstr = (cJSON_IsString(name_j_mc) && name_j_mc->valuestring) ? name_j_mc->valuestring : "";
        if (mod_is_mc_dependent(mstr)) {
          do_mc_fixup = 1;
          cr_log("info", "cheats", "mc_fixup active mc_base_off=0x%llx mc_live=0x%lx mc_on_len=%zu dep='%s'",
                 (unsigned long long)mc_base_off, (long)mc_live_addr, mc_on_len, mstr);
        }
      }
    }
  }

  /* ---- Pre-read pass: parse, validate, back up old bytes, classify ---- */
  rc = 0;
  {
    cJSON *m = NULL;
    cJSON_ArrayForEach(m, memory) {
      if (pre_n >= 64) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] too many patches (max 64)", pre_n);
        break;
      }
      cJSON *off_j      = cJSON_GetObjectItem(m, "offset");
      cJSON *on_j       = cJSON_GetObjectItem(m, "on");
      cJSON *off_j2     = cJSON_GetObjectItem(m, "off");
      cJSON *expected_j = cJSON_GetObjectItem(m, "expected");
      cJSON *abs_j      = cJSON_GetObjectItem(m, "absolute");
      cJSON *sec_j      = cJSON_GetObjectItem(m, "section");
      /* section > 0 resolves the module by handle index; falls back to the eboot base with a warning. */
      intptr_t entry_base = mod_base;
      if (cJSON_IsNumber(sec_j) && sec_j->valuedouble > 0) {
        int sec_num = (int)sec_j->valuedouble;
        intptr_t sec_resolved = kernel_dynlib_mapbase_addr(pid, (uint32_t)sec_num);
        if (sec_resolved > 0) {
          entry_base = sec_resolved;
          cr_log("info", "cheats.mem",
                 "entry[%d] section=%d resolved base=0x%lx title=%s mod=%d",
                 pre_n, sec_num, (long)entry_base, title_id, mod_index);
        } else {
          cr_log("warn", "cheats.mem",
                 "entry[%d] section=%d unresolved (dynlib handle not found) — using eboot base title=%s mod=%d",
                 pre_n, sec_num, title_id, mod_index);
        }
      }
      if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j) || !cJSON_IsString(off_j2)) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] missing offset/on/off", pre_n);
        break;
      }
      size_t need = hex_bytes_need(on_j->valuestring);
      if (hex_bytes_need(off_j2->valuestring) > need) need = hex_bytes_need(off_j2->valuestring);
      if (cJSON_IsString(expected_j) && hex_bytes_need(expected_j->valuestring) > need) need = hex_bytes_need(expected_j->valuestring);
      uint8_t *on_bytes CR_AUTOFREE = malloc(need), *off_bytes CR_AUTOFREE = malloc(need);
      uint8_t *exp_bytes CR_AUTOFREE = malloc(need), *cur_bytes CR_AUTOFREE = malloc(need);
      if (!on_bytes || !off_bytes || !exp_bytes || !cur_bytes) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] out of memory", pre_n);
        break;
      }
      size_t on_len = 0, off_len = 0, exp_len = 0;
      if (parse_hex_bytes_checked(on_j->valuestring, on_bytes, need, &on_len) != 0 ||
          parse_hex_bytes_checked(off_j2->valuestring, off_bytes, need, &off_len) != 0) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] hex parse failed or empty", pre_n);
        break;
      }
      if (on_len != off_len) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] on/off size mismatch (%zu vs %zu)", pre_n, on_len, off_len);
        break;
      }
      if (cJSON_IsString(expected_j) && expected_j->valuestring && expected_j->valuestring[0]) {
        if (parse_hex_bytes_checked(expected_j->valuestring, exp_bytes, need, &exp_len) != 0 ||
            exp_len != on_len) {
          rc = -1;
          snprintf(err, err_size, "entry[%d] expected parse failed/size mismatch", pre_n);
          break;
        }
      }
      uint64_t off_u = 0;
      if (parse_offset_hex_checked(off_j->valuestring, &off_u) != 0) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] invalid offset '%s'", pre_n, off_j->valuestring);
        break;
      }
      intptr_t addr = 0;
      int af = (cJSON_IsTrue(abs_j) || is_ps2) ? 1 : 0;
      /* Keep the raw offset for the master-code scan, then apply the low-byte fixup. */
      uint64_t off_u_dep_raw = off_u;
      if (do_mc_fixup && mc_base_off != 0) {
        off_u = fixup_mc_dependent_addr(mc_base_off, off_u);
      }
      addr = cheat_resolve_addr(pid, entry_base, off_u, af, on_bytes,
                                exp_len > 0 ? exp_bytes : NULL, on_len);

      /* Name heuristic missed it — probe before trusting the naive address. */
      int mc_should_scan = do_mc_fixup;
      if (!mc_should_scan && mc_fallback_available && !af &&
          mc_live_addr > 0 && mc_on_len > 0 && off_len > 0 &&
          on_len > 0 && on_len <= need) {
        if (read_process_memory(pid, addr, cur_bytes, on_len) == 0) {
          int looks_on  = memcmp(cur_bytes, on_bytes, on_len) == 0;
          int looks_off = memcmp(cur_bytes, off_bytes, on_len) == 0;
          if (!looks_on && !looks_off) {
            mc_should_scan = 1;
            cr_log("info", "cheats",
                   "mc_fallback_probe entry[%d] addr=0x%lx matches neither ON nor OFF — "
                   "trying master-code-relative scan title=%s mod=%d",
                   pre_n, (long)addr, title_id, mod_index);
          }
        }
      }
      /* Search the live master-code region for this entry's off bytes. */
      if (mc_should_scan && !af && mc_live_addr > 0 && mc_on_len > 0 && off_len > 0) {
        intptr_t mc_scanned = 0;
        if (mc_scan_dep_addr(pid, mc_live_addr, mc_on_bytes_buf, mc_on_len,
                             off_bytes, off_len, mc_base_off, off_u_dep_raw,
                             &mc_scanned) && mc_scanned != addr) {
          cr_log("info", "cheats",
                 "mc_scan entry[%d] addr=0x%lx→0x%lx title=%s mod=%d",
                 pre_n, (long)addr, (long)mc_scanned, title_id, mod_index);
          addr = mc_scanned;
        }
      }
      const uint8_t *new_data = effective_on ? on_bytes : off_bytes;
      size_t wlen = on_len;
      /* The one gate: skip the entry if the target can't be read. */
      if (!ADDR_IN_USER_RANGE(addr) || read_process_memory(pid, addr, cur_bytes, wlen) != 0) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] target not readable at 0x%lx", pre_n, (long)addr);
        break;
      }
      applied[pre_n].addr = addr;
      applied[pre_n].len  = wlen;
      applied[pre_n].old_bytes = malloc(wlen);
      wi_data[pre_n] = malloc(wlen);
      if (!applied[pre_n].old_bytes || !wi_data[pre_n]) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] out of memory", pre_n);
        break;
      }
      memcpy(applied[pre_n].old_bytes, cur_bytes, wlen);
      memcpy(wi_data[pre_n], new_data, wlen);
      wi_is_cave[pre_n] = (wlen >= 16) ? 1 : 0;
      /* Between entries: abort if the game died while we were reading memory. */
      if (kill(pid, 0) != 0 && errno == ESRCH) {
        rc = -1;
        snprintf(err, err_size, "entry[%d] game exited during pre-read", pre_n);
        cr_log("warn", "cheats.guard",
               "game died mid-apply pid=%d title=%s — aborting and re-polling monitor",
               (int)pid, title_id);
        rpc_refresh_title_and_notify();
        break;
      }
      pre_n++;
    }
  }

  /* A write of >=16 bytes marks its page as a cave page; shorter writes on it follow the same ordering. */
  if (rc == 0 && pre_n > 1) {
    intptr_t cave_pgs[64];
    int cave_pg_n = 0;
    for (int _i = 0; _i < pre_n; _i++) {
      if (wi_is_cave[_i]) {
        intptr_t pg = (intptr_t)ROUND_PG_DOWN((uintptr_t)applied[_i].addr);
        int found = 0;
        for (int _j = 0; _j < cave_pg_n; _j++) if (cave_pgs[_j] == pg) { found = 1; break; }
        if (!found && cave_pg_n < 64) cave_pgs[cave_pg_n++] = pg;
      }
    }
    for (int _i = 0; _i < pre_n && cave_pg_n > 0; _i++) {
      if (!wi_is_cave[_i]) {
        intptr_t pg = (intptr_t)ROUND_PG_DOWN((uintptr_t)applied[_i].addr);
        for (int _j = 0; _j < cave_pg_n; _j++) {
          if (cave_pgs[_j] == pg) {
            wi_is_cave[_i] = 1;
            cr_log("info", "cheats",
                   "entry[%d] promoted to cave by page addr=0x%lx len=%zu title=%s mod=%d",
                   _i, (long)applied[_i].addr, applied[_i].len, title_id, mod_index);
            break;
          }
        }
      }
    }
  }

  /* ---- Write pass: caves first on enable, hooks first on disable ---- */
  if (rc == 0 && pre_n > 0) {
    int cave_count = 0, hook_count = 0;
    for (int _i = 0; _i < pre_n; _i++) {
      if (wi_is_cave[_i]) cave_count++; else hook_count++;
    }
    int hook_codecave = (cave_count > 0 && hook_count > 0);
    if (hook_codecave) {
      cr_log("debug", "cheats.apply",
             "mod=%d pattern=hook_codecave caveWrites=%d hookWrites=%d",
             mod_index, cave_count, hook_count);
      cr_log("debug", "cheats.apply", effective_on ?
             "enable order: caves first, hooks last" :
             "disable order: hooks first, caves last");
    }

    for (int pass = 0; pass < 2 && rc == 0; pass++) {
      if (!hook_codecave && pass == 1) break;
      /* want_cave: for enable pass0=caves, pass1=hooks; for disable pass0=hooks, pass1=caves */
      int want_cave = effective_on ? (pass == 0) : (pass == 1);
      for (int i = 0; i < pre_n && rc == 0; i++) {
        if (hook_codecave && (wi_is_cave[i] ? 1 : 0) != want_cave) continue;
        intptr_t addr = applied[i].addr;
        size_t   wlen = applied[i].len;
        const uint8_t *data = wi_data[i];
        intptr_t _pg = (intptr_t)ROUND_PG_DOWN((uintptr_t)addr);
        size_t   _sp = (size_t)(ROUND_PG_UP((uintptr_t)addr + wlen) - (uintptr_t)_pg);
        cr_log("debug", "cheats.mem",
               "begin title=%s mod=%d write=%d addr=0x%lx len=%zu page=0x%lx span=0x%zx type=%s",
               title_id, mod_index, write_ok_n, (long)addr, wlen, (long)_pg, _sp,
               wi_is_cave[i] ? "cave" : "hook");
        /* Skips kernel_get_vmem_protection, which can dereference garbage and panic on PS4 BC games
         * and PS5 games with special vmem entry types. */
        int wrc = write_process_memory_forced(pid, addr, data, wlen);
        cr_log("debug", "cheats.mem", "write_rc=%d addr=0x%lx", wrc, (long)addr);
        if (wrc != 0) {
          rc = -1;
          snprintf(err, err_size, "entry[%d] write failed at 0x%lx (rc=%d)", i, (long)addr, wrc);
          break;
        }
        {
          /* read_process_memory has its own XOM fallback if the page isn't readable. */
          uint8_t *cur_v CR_AUTOFREE = malloc(wlen);
          int rdrc = cur_v ? read_process_memory(pid, addr, cur_v, wlen) : -1;
          if (rdrc != 0) {
            cr_log("warn", "cheats.mem",
                   "ext_verify_unreadable addr=0x%lx len=%zu — write assumed ok",
                   (long)addr, wlen);
          } else if (memcmp(cur_v, data, wlen) != 0) {
            char exp_h[52] = {0}, got_h[52] = {0};
            fmt_hex16(data, wlen, exp_h, sizeof(exp_h));
            fmt_hex16(cur_v, wlen, got_h, sizeof(got_h));
            cr_log("error", "cheats.mem",
                   "ext_verify_fail title=%s mod=%d write=%d addr=0x%lx exp=[%s] got=[%s]",
                   title_id, mod_index, write_ok_n, (long)addr, exp_h, got_h);
            rc = -1;
            snprintf(err, err_size,
                     "entry[%d] verify failed at 0x%lx — bytes not observed after write; see cheats.mem logs",
                     i, (long)addr);
            break;
          }
        }
        /* x86-64 caches are coherent, no flush needed. restore_rx is an old flag, off by default. */
        cr_log("debug", "cheats.mem",
               "icache_sync addr=0x%lx len=%zu x86_coherent prot_untouched_by_write_fn",
               (long)addr, wlen);
        if (restore_rx && !restore_orig_prot) {
          int rrc = kernel_mprotect(pid, _pg, _sp, PROT_READ | PROT_EXEC);
          cr_log("warn", "cheats.mem",
                 "restore_rx_legacy page=0x%lx span=0x%zx rc=%d (deprecated: use cheat_restore_original_prot)",
                 (long)_pg, _sp, rrc);
        }
        cr_log("info", "cheats", "write[%d] addr=0x%lx len=%zu type=%s ok",
               write_ok_n, (long)addr, wlen, wi_is_cave[i] ? "cave" : "hook");
        written_order[write_ok_n++] = i;
      }
    }
  }

  /* ---- Rollback on failure ---- */
  if (rc != 0 && write_ok_n > 0) {
    cr_log("info", "cheats.rollback", "begin title=%s mod=%d writes_to_undo=%d",
           title_id, mod_index, write_ok_n);
    for (int i = write_ok_n - 1; i >= 0; i--) {
      int wi = written_order[i];
      int rrc = write_process_memory_forced(pid, applied[wi].addr, applied[wi].old_bytes, applied[wi].len);
      if (rrc == 0) {
        uint8_t *chk CR_AUTOFREE = malloc(applied[wi].len);
        int vok = (chk &&
                   read_process_memory(pid, applied[wi].addr, chk, applied[wi].len) == 0 &&
                   memcmp(chk, applied[wi].old_bytes, applied[wi].len) == 0);
        cr_log("info", "cheats.rollback", "write[%d] addr=0x%lx len=%zu rc=0 verify_old=%s",
               i, (long)applied[wi].addr, applied[wi].len, vok ? "ok" : "fail");
      } else {
        cr_log("warn", "cheats.rollback", "write[%d] addr=0x%lx len=%zu rc=%d (write failed)",
               i, (long)applied[wi].addr, applied[wi].len, rrc);
      }
    }
    cr_log("warn", "cheats", "rollback performed for %s mod=%d (%d patches)",
           title_id, mod_index, write_ok_n);
  } else if (rc == 0) {
    cJSON *name_j = cJSON_GetObjectItem(mod, "name");
    const char *mod_name = cJSON_IsString(name_j) && name_j->valuestring ? name_j->valuestring : "mod";
    pthread_mutex_lock(&g_activity_lock);
    snprintf(g_activity_last_cheat_used, sizeof(g_activity_last_cheat_used), "%s:%s", title_id, mod_name);
    int aidx = activity_find_title_index_locked(title_id);
    if (aidx >= 0) {
      snprintf(g_activity_titles[aidx].last_cheat, sizeof(g_activity_titles[aidx].last_cheat), "%s", mod_name);
    }
    pthread_mutex_unlock(&g_activity_lock);
    activity_save();
    notification_add(effective_on ? "cheat_applied" : "cheat_disabled", "Cheat %s: %s #%d",
                     effective_on ? "applied" : "disabled", title_id, mod_index);
    notify("CheatRunner: %s %s", mod_name, effective_on ? "ON" : "OFF");
    if (effective_on) {
      mod_disabled_clear(title_id, mod_index);
      mod_enabled_set(title_id, pid, mod_index);
    } else {
      mod_disabled_set(title_id, pid, mod_index);
      mod_enabled_clear(title_id, mod_index);
    }
    cr_log("info", "cheats", "applied %s mod=%d state=%d appId=0x%x",
           title_id, mod_index, effective_on, app_id_live);
    if (effective_on) {
      int watch_ms = 0, mark_crash = 0;
      pthread_mutex_lock(&g_cfg_lock);
      watch_ms = g_cfg.cheat_post_apply_watch_ms;
      mark_crash = g_cfg.cheat_mark_crash_suspect;
      pthread_mutex_unlock(&g_cfg_lock);
      if (mark_crash && watch_ms > 0) {
        int _hcc = 0, _hch = 0;
        for (int _i = 0; _i < pre_n; _i++) { if (wi_is_cave[_i]) _hcc++; else _hch++; }
        pthread_mutex_lock(&g_crash_guard_lock);
        {
          /* Replace existing entry for same mod, or append a new one */
          int _gi = -1;
          for (int _k = 0; _k < g_crash_guard_n; _k++) {
            if (g_crash_guard_arr[_k].mod_index == mod_index &&
                strcmp(g_crash_guard_arr[_k].title_id, title_id) == 0) {
              _gi = _k; break;
            }
          }
          if (_gi < 0 && g_crash_guard_n < CRASH_GUARD_MAX) _gi = g_crash_guard_n++;
          if (_gi >= 0) {
            snprintf(g_crash_guard_arr[_gi].title_id, sizeof(g_crash_guard_arr[_gi].title_id), "%s", title_id);
            g_crash_guard_arr[_gi].mod_index    = mod_index;
            snprintf(g_crash_guard_arr[_gi].mod_name, sizeof(g_crash_guard_arr[_gi].mod_name), "%s", mod_name);
            g_crash_guard_arr[_gi].pid          = pid;
            g_crash_guard_arr[_gi].enabled_at_ms = now_ms();
          }
        }
        g_post_apply_guard_until_ms = now_ms() + (uint64_t)watch_ms;
        pthread_mutex_unlock(&g_crash_guard_lock);
        cr_log("info", "cheats.guard",
               "monitoring title=%s pid=%d appId=0x%x mod=%d name=\"%s\" for %dms hc=%d",
               title_id, (int)pid, app_id_live, mod_index, mod_name, watch_ms,
               (_hcc > 0 && _hch > 0) ? 1 : 0);
      }
    } else {
      pthread_mutex_lock(&g_crash_guard_lock);
      for (int _k = g_crash_guard_n - 1; _k >= 0; _k--) {
        if (strcmp(g_crash_guard_arr[_k].title_id, title_id) == 0 &&
            g_crash_guard_arr[_k].mod_index == mod_index) {
          g_crash_guard_arr[_k] = g_crash_guard_arr[--g_crash_guard_n];
          break;
        }
      }
      if (g_crash_guard_n == 0) g_post_apply_guard_until_ms = 0;
      pthread_mutex_unlock(&g_crash_guard_lock);
    }
  }

  {
    cJSON *name_j2 = cJSON_GetObjectItem(mod, "name");
    const char *mn = cJSON_IsString(name_j2) && name_j2->valuestring ? name_j2->valuestring : "mod";
    int hcc = 0, hch = 0;
    for (int _i = 0; _i < pre_n; _i++) { if (wi_is_cave[_i]) hcc++; else hch++; }
    pthread_mutex_lock(&g_last_apply_lock);
    memset(&g_last_apply_rec, 0, sizeof(g_last_apply_rec));
    snprintf(g_last_apply_rec.title_id,  sizeof(g_last_apply_rec.title_id),  "%s", title_id);
    snprintf(g_last_apply_rec.mod_name,  sizeof(g_last_apply_rec.mod_name),  "%s", mn);
    g_last_apply_rec.mod_index    = mod_index;
    g_last_apply_rec.effective_on = effective_on;
    g_last_apply_rec.ok           = (rc == 0) ? 1 : 0;
    g_last_apply_rec.hook_codecave= (hcc > 0 && hch > 0) ? 1 : 0;
    g_last_apply_rec.cave_count   = hcc;
    g_last_apply_rec.hook_count   = hch;
    g_last_apply_rec.ts_ms        = now_ms();
    int wc = write_ok_n < LAST_APPLY_WRITES_MAX ? write_ok_n : LAST_APPLY_WRITES_MAX;
    for (int _i = 0; _i < wc; _i++) {
      int wi = written_order[_i];
      g_last_apply_rec.writes[_i].addr    = applied[wi].addr;
      g_last_apply_rec.writes[_i].len     = applied[wi].len;
      g_last_apply_rec.writes[_i].is_cave = wi_is_cave[wi];
    }
    g_last_apply_rec.write_count = wc;
    pthread_mutex_unlock(&g_last_apply_lock);
  }

  g_cheat_applying = 0;
  g_last_apply_at_ms = now_ms();
  cr_log("info", "cheats.guard", "resuming scanners after apply title=%s mod=%d rc=%d",
         title_id, mod_index, rc);

  pthread_mutex_unlock(&g_cheat_apply_lock);
  cJSON_Delete(root);
  if (rc == 0 && effective_on && inter_mod_delay_ms > 0) {
    usleep((useconds_t)((unsigned int)inter_mod_delay_ms * 1000u));
    pid_t live_pid = (app_id_live > 0) ? find_pid_for_app_id((uint32_t)app_id_live) : -1;
    if (live_pid <= 0) {
      cr_log("warn", "cheats.guard",
             "inter_mod_check: game not detected %dms after enabling mod=%d title=%s",
             inter_mod_delay_ms, mod_index, title_id);
      return -4;
    }
  }
  return rc;
}

/* The per-entry byte buffers live on the heap, since some entries run to hundreds of KB. */
int
apply_cheat_json(const char *title_id, int mod_index, int turn_on, char *err, size_t err_size) {
  applied_patch_t *applied = calloc(64, sizeof(*applied));
  uint8_t **wi_data = calloc(64, sizeof(*wi_data));
  if (!applied || !wi_data) {
    free(applied);
    free(wi_data);
    snprintf(err, err_size, "out of memory");
    return -1;
  }
  int rc = apply_cheat_json_impl(title_id, mod_index, turn_on, err, err_size, applied, wi_data);
  for (int i = 0; i < 64; i++) {
    free(applied[i].old_bytes);
    free(wi_data[i]);
  }
  free(applied);
  free(wi_data);
  return rc;
}


