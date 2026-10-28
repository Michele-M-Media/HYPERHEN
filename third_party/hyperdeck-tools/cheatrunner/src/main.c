#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "jb.h"
#include "priv_bootstrap.h"
#include "ps5sdk_compat.h"
#include "cr_activity.h"
#include "cr_config.h"
#include "cr_game_monitor.h"
#include "cr_hotkey_hook.h"
#include "cr_http.h"
#include "cr_launch.h"
#include "cr_log.h"
#include "cr_notifications.h"
#include "cr_paths.h"
#include "cr_shutdown.h"
#include "cr_cheats.h"
#include "cr_cheat_profiles.h"
#include "cr_patch_profiles.h"
#include "cr_favorites.h"
#include "cr_tile_pkg.h"
#include "build_id.h"
#ifndef CHEATRUNNER_VERSION
#define CHEATRUNNER_VERSION "0.18"
#endif

/* Async-signal-safe crash handler: writes a one-line entry to the crash log
 * using only open/write/close/_exit (no malloc, no stdio). */
static void
crash_sighandler(int sig) {
  static const char path[] = CHEATRUNNER_CRASH_LOG_PATH;
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    static const char hdr[]  = "signal=";
    static const char tail[] = " CheatRunner crashed\n";
    int s = (sig < 0 ? 0 : (sig > 99 ? 99 : sig));
    char nb[2];
    nb[0] = (char)('0' + s / 10);
    nb[1] = (char)('0' + s % 10);
    (void)write(fd, hdr,  sizeof(hdr) - 1);
    (void)write(fd, nb,   2);
    (void)write(fd, tail, sizeof(tail) - 1);
    close(fd);
  }
  _exit(sig);
}

/* Older builds let the tile-installer thread rename the whole process, so look for both names. */
static pid_t
find_old_instance(void) {
  static const char *const names[] = { "CheatRunner.elf", "cr-tile-inst" };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    pid_t pid = find_pid_by_name(names[i]);
    if (pid > 0) return pid;
  }
  return -1;
}

/* True if something accepts connections on the loopback port. */
static int
port_answers(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int up = connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0;
  close(fd);
  return up;
}

/* An older CheatRunner can outlive the kill-by-name pass and keep serving its old dashboard; ask each one to exit. */
static void
evict_old_instance(int port) {
  for (int round = 0; round < 2 && port_answers(port); round++) {
    cr_log("warn", "boot", "an older instance answers on port %d, asking it to exit", port);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
      static const char req[] = "POST /api/dev/shutdown HTTP/1.0\r\nContent-Length: 0\r\n\r\n";
      char junk[256];
      (void)send(fd, req, sizeof(req) - 1, 0);
      (void)recv(fd, junk, sizeof(junk), 0);
    }
    close(fd);
    /* The old instance exits after its shutdown delay; give it time to drop the port. */
    for (int i = 0; i < 5; i++) usleep(300 * 1000);
  }
  if (port_answers(port)) {
    cr_log("error", "boot", "older instance is still answering; the dashboard may show the old version");
  }
}

int
main(void) {
  pid_t old_pid;
  signal(SIGPIPE, SIG_IGN);
  {
    struct sigaction sa;
    sa.sa_handler = crash_sighandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
  }

  syscall(SYS_thr_set_name, -1, "CheatRunner.elf");

  cr_log_klog_banner();

  puts("   ____ _                _   ____                              ");
  puts("  / ___| |__   ___  __ _| |_|  _ \\ _   _ _ __  _ __   ___ _ __ ");
  puts(" | |   | '_ \\ / _ \\/ _` | __| |_) | | | | '_ \\| '_ \\ / _ \\ '__|");
  puts(" | |___| | | |  __/ (_| | |_|  _ <| |_| | | | | | | |  __/ |   ");
  puts("  \\____|_| |_|\\___|\\__,_|\\__|_| \\_\\\\__,_|_| |_|_| |_|\\___|_|   ");
  log_msg("version: %s (build %s)", CHEATRUNNER_VERSION, CHEATRUNNER_BUILD_ID);

#ifdef __SCE__
  if (jb_escalate_pid(getpid()) != 0) {
    cr_log("warn", "core", "jb_escalate_pid failed; privilege may be incomplete");
  }
  cr_priv_init();
  {
    const cr_priv_status_t *ps = cr_priv_get();
    cr_log("info", "priv",
           "uid=%d gid=%d euid=%d egid=%d uid_ok=%d sandbox=%d authid_ok=%d caps_ok=%d kernel_rw=%d mprotect=%d can_patch=%d",
           ps->uid, ps->gid, ps->euid, ps->egid, ps->uid_ok, ps->sandbox_ok, ps->authid_ok, ps->caps_ok,
           ps->kernel_rw_ok, ps->kernel_mprotect_ok, ps->can_patch_game);
    if (!ps->can_patch_game) {
      cr_log("warn", "priv", "privilege bootstrap incomplete; cheat writes may fail");
      for (int i = 0; i < ps->n_warnings; i++) {
        cr_log("warn", "priv", "  %s", ps->warnings[i]);
      }
    }
  }
#endif

  /* Kill any previous instance before binding. EPERM (the old one escalated, we did not) breaks fast,
   * ESRCH means gone, and 60 spins is the cap so a zombie cannot loop forever. */
  {
    int kill_spins = 0;
    while ((old_pid = find_old_instance()) > 0 && kill_spins < 60) {
      if (kill_spins == 0) cr_log("info", "boot", "killing previous instance pid=%d", old_pid);
      int kerr = kill(old_pid, SIGKILL);
      if (kerr != 0) {
        int e = errno;
        cr_log("warn", "boot", "SIGKILL pid=%d errno=%d", old_pid, e);
        if (e == EPERM) {
          cr_log("error", "boot",
                 "cannot kill old instance (EPERM) — escalation may have failed");
          break;
        }
        if (e == ESRCH) break;  /* already dead */
      }
      usleep(150 * 1000);
      kill_spins++;
    }
    if (old_pid > 0 && kill_spins >= 60) {
      cr_log("warn", "boot", "old instance pid=%d still visible after 9s (zombie?)", old_pid);
    }
  }

  {
    int prio = 256;
    int urc = sceUserServiceInitialize(&prio);
    if (urc != 0 && urc != (int)0x80960003) {
      cr_log("warn", "core", "sceUserServiceInitialize rc=0x%08X", (uint32_t)urc);
    }
  }

  ensure_data_dirs();
  config_load();
  pthread_mutex_lock(&g_cfg_lock);
  int boot_port = g_cfg.http_port;
  pthread_mutex_unlock(&g_cfg_lock);
  evict_old_instance(boot_port);
  notifications_load();
  activity_load();
  crash_suspects_load();
  favorites_load();
  cheat_profiles_load();
  patch_profiles_load();
  cr_tile_autoinstall_init();
  rpc_refresh_title_and_notify();
  set_launch_status_ex(0, "idle", "", "ready", 1, "", 0, 0);
  notify("CheatRunner v" CHEATRUNNER_VERSION " by maj0r");
  notification_add("boot", "CheatRunner v" CHEATRUNNER_VERSION " started");
  cr_log("info", "boot", "version %s", CHEATRUNNER_VERSION);

  pthread_t http_thread;
  pthread_t monitor_thread;
  pthread_t net_watchdog_thread;
  pthread_t hotkey_thr;
  if (pthread_create(&http_thread, NULL, http_server_thread, NULL) != 0) {
    log_msg("error: could not start HTTP thread");
    notify("CheatRunner HTTP thread failed");
    return 1;
  }
  if (pthread_create(&net_watchdog_thread, NULL, http_net_watchdog_thread, NULL) != 0) {
    log_msg("error: could not start network watchdog thread");
    notify("CheatRunner network watchdog thread failed");
    return 1;
  }
  if (pthread_create(&monitor_thread, NULL, game_monitor_thread, NULL) != 0) {
    log_msg("error: could not start game monitor thread");
    notify("CheatRunner game monitor thread failed");
    return 1;
  }
  if (pthread_create(&hotkey_thr, NULL, hotkey_hook_thread, NULL) != 0) {
    log_msg("error: could not start hotkey thread");
    notify("CheatRunner hotkey thread failed");
    return 1;
  }

  pthread_join(http_thread, NULL);
  g_game_monitor_running = 0;
  g_hotkey_hook_running = 0;
  pthread_join(monitor_thread, NULL);
  pthread_join(net_watchdog_thread, NULL);
  pthread_join(hotkey_thr, NULL);
  return 0;
}
