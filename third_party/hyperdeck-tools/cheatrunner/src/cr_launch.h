#ifndef CR_LAUNCH_H
#define CR_LAUNCH_H

#include <pthread.h>
#include <stdint.h>

typedef struct launch_status_state {
  int busy;
  char phase[32];
  char title_id[16];
  char message[160];
  int last_ok;
  char method[40];
  int rc;
  int verified;
  char hex[16];
  int foreground_user_valid;
  uint64_t started_ms;   /* monotonic ms when current launch began */
  uint64_t updated_ms;   /* monotonic ms of last status write */
  uint64_t generation;   /* incremented each time a new launch starts */
} launch_status_state_t;

typedef struct launch_worker_request {
  char title_id[16];
  char args[512];
  uint64_t generation;   /* matches g_launch_status.generation at dispatch */
} launch_worker_request_t;

extern pthread_mutex_t g_launch_status_lock;
extern launch_status_state_t g_launch_status;
extern uint64_t g_last_launch_verified_at_ms;

/* Phases that indicate a launch is in progress (busy=1 expected). */
static inline int launch_phase_is_busy(const char *phase) {
  if (!phase || !*phase) return 0;
  return !__builtin_strcmp(phase, "queued")          ||
         !__builtin_strcmp(phase, "killing_current") ||
         !__builtin_strcmp(phase, "waiting_for_close")||
         !__builtin_strcmp(phase, "launching_lnc")   ||
         !__builtin_strcmp(phase, "verifying_lnc")   ||
         !__builtin_strcmp(phase, "launching_system") ||
         !__builtin_strcmp(phase, "verifying_system");
}

/* Terminal phases: launch has ended (success or failure). */
static inline int launch_phase_is_terminal(const char *phase) {
  if (!phase || !*phase) return 0;
  return !__builtin_strcmp(phase, "idle")            ||
         !__builtin_strcmp(phase, "ready")           ||
         !__builtin_strcmp(phase, "failed")          ||
         !__builtin_strcmp(phase, "timeout")         ||
         !__builtin_strcmp(phase, "already_running") ||
         !__builtin_strcmp(phase, "stale_recovered");
}

void set_launch_status(int busy, const char *phase, const char *title_id, const char *message, int last_ok);
void set_launch_status_ex(int busy, const char *phase, const char *title_id, const char *message, int last_ok,
                          const char *method, int rc, int verified);
/* Begin a new launch: atomically increments generation, sets busy, returns generation via gen_out */
void launch_begin_ex(const char *title_id, uint64_t *gen_out);
/* Generation-guarded status update: no-op if gen != g_launch_status.generation */
void set_launch_status_ex_gen(uint64_t gen, int busy, const char *phase, const char *title_id,
                              const char *message, int last_ok, const char *method, int rc, int verified);
/* Stale watchdog: if launch has been busy longer than threshold, auto-recovers. Returns 1 if recovered. */
int launch_status_recover_stale(void);
/* Called when a game starts: moves launch_status from "timeout"/"failed" back to "ready" for this title. */
void launch_status_recover_for_game(const char *title_id);
void *launch_worker_thread(void *arg);

#endif
