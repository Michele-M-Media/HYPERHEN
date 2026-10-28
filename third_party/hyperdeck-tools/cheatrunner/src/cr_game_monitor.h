#ifndef CR_GAME_MONITOR_H
#define CR_GAME_MONITOR_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

typedef struct running_game_state {
  int running;
  int app_id;
  pid_t pid;
  intptr_t image_base;
  char title_id[16];
  char title_name[256];
  char platform[8];
  char eboot_path[256];
  char content_version[64];
  char app_version[64];
  uint64_t started_at;
} running_game_state_t;

pid_t find_pid_for_app_id(uint32_t app_id);
pid_t find_pid_by_name(const char *name);
int get_running_game_ex(pid_t *out_pid, char *out_title, size_t title_size, intptr_t *out_base, int *out_app_id);
int get_running_game(pid_t *out_pid, char *out_title, size_t title_size, intptr_t *out_base);

void running_state_set(const running_game_state_t *st);
void running_state_get(running_game_state_t *out);
int read_running_state(running_game_state_t *out);

void set_current_title(const char *title);
void get_current_title(char *out, size_t out_size);

extern volatile int g_game_monitor_running;
void rpc_refresh_title_and_notify(void);
void *game_monitor_thread(void *arg);

#endif /* CR_GAME_MONITOR_H */
