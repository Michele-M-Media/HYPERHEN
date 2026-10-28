#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cr_log.h"
#include "cr_activity.h"
#include "cr_notifications.h"
#include "cr_shutdown.h"

volatile int g_shutdown_requested = 0;
extern volatile int g_game_monitor_running;
extern int g_http_listen_fd;
/* Held by cr_cheats.c during a cheat apply/disable's writes. */
extern pthread_mutex_t g_cheat_apply_lock;

static void
close_fd_safe(int *fd) {
  if (!fd || *fd < 0) {
    return;
  }
  shutdown(*fd, SHUT_RDWR);
  close(*fd);
  *fd = -1;
}

void
cheatrunner_close_servers(void) {
  close_fd_safe(&g_http_listen_fd);
}

/* Wait briefly for an in-flight apply, then kill. Never block: a stuck apply (hung mdbg/ptrace)
 * must not stop shutdown from reaching SIGKILL. */
static void
kill_after_no_apply_in_flight(void) {
  for (int waited_ms = 0; waited_ms < 2000; waited_ms += 50) {
    if (pthread_mutex_trylock(&g_cheat_apply_lock) == 0) {
      break;
    }
    usleep(50 * 1000);
  }
  kill(getpid(), SIGKILL);
  _exit(0);
}

static void *
delayed_shutdown_thread(void *arg) {
  int delay_ms = arg ? *(int *)arg : 700;
  free(arg);
  if (delay_ms < 0) {
    delay_ms = 0;
  }
  usleep((useconds_t)delay_ms * 1000);
  cr_log("info", "dev", "shutdown requested; closing CheatRunner");
  g_shutdown_requested = 1;
  g_game_monitor_running = 0;
  cr_log("info", "dev", "closing HTTP socket");
  cheatrunner_close_servers();
  activity_save();
  notifications_save();
  cr_log("info", "dev", "CheatRunner exiting for reload");
  /* Guaranteed exit: this always reaches SIGKILL/_exit, so a shutdown request never leaves CheatRunner running. */
  kill_after_no_apply_in_flight();
  return NULL;
}

void
cheatrunner_request_shutdown(int delay_ms) {
  int *p = malloc(sizeof(*p));
  if (!p) {
    g_shutdown_requested = 1;
    g_game_monitor_running = 0;
    cheatrunner_close_servers();
    kill_after_no_apply_in_flight();
    return;
  }
  *p = delay_ms;
  pthread_t t;
  if (pthread_create(&t, NULL, delayed_shutdown_thread, p) == 0) {
    pthread_detach(t);
    return;
  }
  free(p);
  g_shutdown_requested = 1;
  g_game_monitor_running = 0;
  cheatrunner_close_servers();
  kill_after_no_apply_in_flight();
}
