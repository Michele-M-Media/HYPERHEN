#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/event.h>
#include <sys/time.h>

#include <ps5/kernel.h>

#include "cr_hotkey_hook.h"
#include "cr_browser.h"
#include "cr_config.h"
#include "cr_game_monitor.h"
#include "cr_hotkey_buttons.h"
#include "cr_hotkey_signal.h"
#include "cr_log.h"
#include "cr_notifications.h"
#include "cr_paths.h"
#include "elfldr.h"
#include "priv_bootstrap.h"
#include "ps5sdk_compat.h"
#include "pt.h"

#include "cr_getdata_payload_blob.h"

volatile int g_hotkey_hook_running = 1;

/* Stop after this many hooks in a row that SceShellUI restarts within CR_HOTKEY_RESTART_WINDOW_MS of;
 * the hook is the likely cause, so require a relaunch. */
#define CR_HOTKEY_MAX_REINJECTS 5
#define CR_HOTKEY_RESTART_WINDOW_MS 120000

/* On some firmwares (4.xx/5.xx) a second PT_ATTACH on the same pid never succeeds; back off after a few. */
#define CR_HOTKEY_MAX_ATTACH_FAILS 3

/* Nothing fixes a firmware the hook cannot install on, and retrying forever is worse than no hotkey. */
#define CR_HOTKEY_MAX_TOTAL_FAILS 4

/* SceShellUI that vanished after an unanswered injection this many times: stop touching it. */
#define CR_HOTKEY_MAX_SHELL_LOSSES 2

/* Wait this long after the libraries load, so other tools patching the new shell finish first. */
#define CR_HOTKEY_SETTLE_AFTER_LIBS_MS 10000

/* Cheap (one find_pid_by_name call), so it can poll often and catch a SceShellUI restart fast. */
#define CR_HOTKEY_POLL_MS 3000

/* Pause after repeated failures on one SceShellUI process. */
#define CR_HOTKEY_BACKOFF_MS 20000

/* Waits between injection attempts on the same SceShellUI process. */
static const int k_retry_delay_ms[] = {500, 1000, 2000, 5000};

#ifdef __SCE__
/* Pushes combo/hold_ms from g_cfg to the payload through a throwaway socket. */
static void
broadcast_hotkey_config(void) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CR_HOTKEY_CONFIG_SIGNAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  pthread_mutex_lock(&g_cfg_lock);
  cr_hotkey_config_packet_t pkt;
  pkt.combo = cr_hotkey_button_value(g_cfg.hotkey_button_a) | cr_hotkey_button_value(g_cfg.hotkey_button_b);
  pkt.hold_ms = (uint32_t)g_cfg.hotkey_hold_ms;
  pthread_mutex_unlock(&g_cfg_lock);

  sendto(fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, sizeof(addr));
  close(fd);
}

int sceKernelOpenEventFlag(intptr_t *flag, const char *name);
int sceKernelPollEventFlag(intptr_t flag, uint64_t bits, unsigned int mode, uint64_t *result);
int sceKernelCloseEventFlag(intptr_t flag);

/* True while the console is suspending or in standby; attaching to SceShellUI then can freeze it. */
static int
console_sleeping(void) {
  static intptr_t flag = -1;
  uint64_t bits = 0;

  if (flag < 0 && sceKernelOpenEventFlag(&flag, "SceSystemStateMgrInfo") < 0) {
    flag = -1;
    return 0;
  }
  if (sceKernelPollEventFlag(flag, UINT64_MAX, 2, &bits) < 0) {
    sceKernelCloseEventFlag(flag);
    flag = -1;
    return 0;
  }
  unsigned int state = (unsigned int)(bits & 0xffff);
  return state == 300 || state == 500;
}

int sceKernelGetProcessName(int pid, char *name);

/* Bumped when the kernel reports a fresh SceShellUI image, even if its pid was reused. */
static volatile int g_shellui_exec_seq;

static void *
shellui_exec_watch(void *arg) {
  (void)arg;
  int kq = kqueue();
  if (kq < 0) {
    cr_log("warn", "hotkey_hook", "no kqueue, new SceShellUI processes are found by polling only");
    return NULL;
  }
  pid_t syscore = -1;
  while (g_hotkey_hook_running) {
    if (syscore <= 0) {
      struct kevent reg;
      syscore = find_pid_by_name("SceSysCore.elf");
      if (syscore > 0) {
        EV_SET(&reg, (uintptr_t)syscore, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, NULL);
        if (kevent(kq, &reg, 1, NULL, 0, NULL) != 0) {
          syscore = -1;
        }
      }
      if (syscore <= 0) {
        sleep(2);
        continue;
      }
      cr_log("debug", "hotkey_hook", "watching SceSysCore (pid=%d) for a new SceShellUI", (int)syscore);
    }
    struct kevent ev;
    struct timespec wait = {1, 0};
    if (kevent(kq, NULL, 0, &ev, 1, &wait) <= 0) {
      continue;
    }
    if (ev.flags & EV_ERROR) {
      syscore = -1;
      continue;
    }
    if (ev.fflags & NOTE_EXEC) {
      char name[64] = {0};
      app_info_t info;
      int name_rc = sceKernelGetProcessName((int)ev.ident, name);
      int info_rc = sceKernelGetAppInfo((pid_t)ev.ident, &info);
      cr_log("debug", "hotkey_hook", "exec event pid=%d name=\"%s\" (rc=%d) title=%.9s (rc=%d)", (int)ev.ident,
             name_rc == 0 ? name : "", name_rc, info_rc == 0 ? info.title_id : "", info_rc);
      if (info_rc == 0 && !strncmp(info.title_id, "NPXS40087", 9)) {
        /* The shell's web child processes share its title id; only the process named SceShellUI counts. */
        int is_shell = 0;
        for (int i = 0; i < 20 && g_hotkey_hook_running && !is_shell; i++) {
          is_shell = find_pid_by_name("SceShellUI") == (pid_t)ev.ident;
          if (!is_shell) {
            usleep(250 * 1000);
          }
        }
        if (is_shell) {
          cr_log("debug", "hotkey_hook", "SceShellUI image started (pid=%d)", (int)ev.ident);
          __atomic_add_fetch(&g_shellui_exec_seq, 1, __ATOMIC_RELEASE);
        }
      }
    }
    if ((ev.fflags & NOTE_EXIT) && (pid_t)ev.ident == syscore) {
      syscore = -1;
    }
  }
  close(kq);
  return NULL;
}

/* Gate: libraries are loaded, then the shell must stay up this long before it is touched. */
static pid_t g_settle_pid = -1;
static uint64_t g_settle_since_ms;

static int
shellui_settled(pid_t pid, uint64_t now) {
  if (g_settle_pid != pid) {
    g_settle_pid = pid;
    g_settle_since_ms = now;
    cr_log("info", "hotkey_hook", "SceShellUI (pid=%d) is up, waiting %d s before hooking it", (int)pid,
           CR_HOTKEY_SETTLE_AFTER_LIBS_MS / 1000);
  }
  return now - g_settle_since_ms >= CR_HOTKEY_SETTLE_AFTER_LIBS_MS;
}

static void
notify_hotkey_off(void) {
  notify("CheatRunner: couldn't hook the system UI, the controller shortcut is off.");
}

/* Arms the jumps the payload could not arm itself, with SceShellUI frozen so no thread can be running
 * those bytes. */
static int
arm_hook_sites(pid_t pid, const cr_hook_request_t *req) {
  if (req->count == 0 || req->count > CR_HOOK_MAX_SITES) {
    cr_log("warn", "hotkey_hook", "hook request has %u sites, ignoring", req->count);
    return -1;
  }

  int written = 0;
  int kinds = 0;
  uint32_t pending = 0;
  for (uint32_t i = 0; i < req->count; i++) {
    if (req->sites[i].done) {
      written++;
      kinds |= 1 << req->sites[i].kind;
    } else {
      pending++;
    }
  }
  cr_log("debug", "hotkey_hook", "%d of %u site(s) were armed by the payload", written, req->count);

  if (pending) {
    /* One credential window for the whole attach, write and detach. */
    int batch = pt_batch_begin() == 0;
    cr_log("debug", "hotkey_hook", "arming %u site(s) in pid=%d, credential window %s", pending,
           (int)pid, batch ? "open" : "unavailable");

    /* PT_ATTACH already stops every thread, which is all we need here. */
    if (pt_attach_timed(pid, 2000) != 0) {
      if (batch) {
        pt_batch_end();
      }
      cr_log("warn", "hotkey_hook", "couldn't stop SceShellUI (pid=%d) to arm the hooks", (int)pid);
      return written > 0 ? 0 : -1;
    }
    cr_log("debug", "hotkey_hook", "SceShellUI (pid=%d) stopped for arming", (int)pid);

    for (uint32_t i = 0; i < req->count; i++) {
      const cr_hook_site_t *s = &req->sites[i];
      if (s->done || s->len == 0 || s->len > CR_HOOK_MAX_BYTES) {
        continue;
      }
      /* PT_IO, not mdbg: mdbg writes the physical page, so patching a shared library leaked into other
       * processes. PT_IO faults it in copy-on-write, so only SceShellUI sees the hook. */
      if (pt_copyin(pid, s->bytes, (intptr_t)s->target, s->len) == 0) {
        cr_log("debug", "hotkey_hook", "site %u kind=%u armed at 0x%llx (%u bytes)", i, (unsigned)s->kind,
               (unsigned long long)s->target, s->len);
        written++;
        kinds |= 1 << s->kind;
      } else {
        cr_log("warn", "hotkey_hook", "couldn't write the hook at 0x%llx",
               (unsigned long long)s->target);
      }
    }

    if (pt_detach(pid, 0) != 0) {
      cr_log("error", "hotkey_hook", "PT_DETACH after arming failed - SceShellUI (pid=%d) "
                                      "may be left stopped", (int)pid);
    }
    if (batch) {
      pt_batch_end();
    }
    cr_log("debug", "hotkey_hook", "SceShellUI (pid=%d) detached after arming", (int)pid);
  }

  cr_log("info", "hotkey_hook", "armed %d/%u hook site(s) in SceShellUI (pid=%d)%s",
         written, req->count, (int)pid, pending ? "" : ", no second attach needed");
  if (kinds != ((1 << CR_HOOK_KIND_GETDATA) | (1 << CR_HOOK_KIND_MENU) | (1 << CR_HOOK_KIND_BOOT))) {
    cr_log("warn", "hotkey_hook", "incomplete hooks (getdata=%d menu=%d boot=%d) - the ★ menu entry may not work",
           kinds & 1, (kinds >> 1) & 1, (kinds >> 2) & 1);
  }
  return written > 0 ? 0 : -1;
}

static pid_t
read_ready_pid(void) {
  char buf[16] = {0};
  int fd = open(CR_HOTKEY_READY_FILE, O_RDONLY);
  if (fd < 0) {
    return -1;
  }
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  return n > 0 ? (pid_t)atoi(buf) : -1;
}

static void
write_ready_pid(pid_t pid) {
  char buf[16];
  int n = snprintf(buf, sizeof(buf), "%d", (int)pid);
  mkdir("/system_tmp/cheatrunner", 0777);
  int fd = open(CR_HOTKEY_READY_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd >= 0) {
    (void)!write(fd, buf, (size_t)n);
    close(fd);
  }
  cr_log("debug", "hotkey_hook", "ready file %s for pid=%d", fd >= 0 ? "written" : "could not be written", (int)pid);
}

/* The shell is up before its libraries (after rest mode, older firmwares); hooking then finds no Mono. */
#define CR_SHELLUI_LIBS_WAIT_MS 120000

static int
shellui_libs_loaded(pid_t pid) {
  static const char *libs[] = {"libSceNpTrophy.sprx", "libSceNpTrophy2.sprx"};
  for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); i++) {
    uint32_t handle = (uint32_t)-1;
    if (kernel_dynlib_handle(pid, libs[i], &handle) != 0 || handle == (uint32_t)-1) {
      cr_log("debug", "hotkey_hook", "pid=%d: %s is not loaded yet", (int)pid, libs[i]);
      return 0;
    }
  }
  return 1;
}

static int
shell_lib_loaded(pid_t pid, const char *lib) {
  uint32_t handle = (uint32_t)-1;
  return kernel_dynlib_handle(pid, lib, &handle) == 0 && handle != (uint32_t)-1;
}

/* Mono is always required; the other libraries are only waited for up to a limit. */
static int
shellui_ready(pid_t pid) {
  static pid_t waiting_pid = -1;
  static uint64_t waiting_since = 0;
  uint64_t now = now_ms();

  if (waiting_pid != pid) {
    waiting_pid = pid;
    waiting_since = now;
    cr_log("info", "hotkey_hook", "waiting for SceShellUI (pid=%d) to finish loading", (int)pid);
  }
  if (!shell_lib_loaded(pid, "libmonosgen-2.0.sprx")) {
    cr_log("debug", "hotkey_hook", "pid=%d: libmonosgen-2.0.sprx is not loaded yet", (int)pid);
    return 0;
  }
  if (shellui_libs_loaded(pid)) {
    return 1;
  }
  if (now - waiting_since >= CR_SHELLUI_LIBS_WAIT_MS) {
    cr_log("warn", "hotkey_hook", "SceShellUI libraries still missing after %ds - hooking anyway",
           CR_SHELLUI_LIBS_WAIT_MS / 1000);
    return 1;
  }
  return 0;
}

/* elfldr_exec only means the thread spawned; wait for the payload to confirm it
 * (covers Mono resolution and the menu-hook retries). */
#define CR_HOTKEY_CONFIRM_TIMEOUT_MS 40000

/* The payload ran but failed or never answered; injecting again can crash the shell. */
#define CR_INJECT_PAYLOAD_FAILED (-2)
#define CR_HOTKEY_MAX_PAYLOAD_FAILS 2

static int
inject_hook(pid_t pid, int sig_fd) {
  /* The credential window covers attach and injection only, not the wait for the payload. */
  int batch = pt_batch_begin() == 0;
  cr_log("debug", "hotkey_hook", "injecting into pid=%d, credential window %s", (int)pid,
         batch ? "open" : "unavailable");
  int rc = pt_attach_timed(pid, 2000);
  if (rc != 0) {
    if (batch) {
      pt_batch_end();
    }
    cr_log("warn", "hotkey_hook", "couldn't attach to SceShellUI (pid=%d, rc=%d)", (int)pid, rc);
    return -1;
  }

  cr_log("debug", "hotkey_hook", "attached to pid=%d, starting the payload", (int)pid);
  uint64_t exec_start = now_ms();
  rc = elfldr_exec(pid, -1, (uint8_t *)g_cr_getdata_payload_elf);
  if (batch) {
    pt_batch_end();
  }
  cr_log("debug", "hotkey_hook", "elfldr_exec returned %d after %llu ms", rc,
         (unsigned long long)(now_ms() - exec_start));
  if (rc != 0) {
    cr_log("warn", "hotkey_hook", "couldn't hook into SceShellUI (pid=%d, rc=%d)", (int)pid, rc);
    return -1;
  }

  /* Room for a full hook request or a forwarded log line, plus a closing NUL. */
  union {
    cr_hook_request_t req;
    char str[CR_LOG_SIGNAL_MAX + CR_LOG_SIGNAL_PREFIX_LEN + 1];
  } msg;
  cr_log("debug", "hotkey_hook", "waiting up to %d s for the payload's answer", CR_HOTKEY_CONFIRM_TIMEOUT_MS / 1000);
  uint64_t wait_start = now_ms();
  ssize_t n = 0;

  /* The payload's own log lines arrive first; keep reading until the real answer. */
  for (;;) {
    uint64_t waited = now_ms() - wait_start;
    if (waited >= CR_HOTKEY_CONFIRM_TIMEOUT_MS) {
      n = 0;
      break;
    }
    uint64_t left = CR_HOTKEY_CONFIRM_TIMEOUT_MS - waited;
    struct timeval tv;
    tv.tv_sec = left / 1000;
    tv.tv_usec = (left % 1000) * 1000;
    setsockopt(sig_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&msg, 0, sizeof(msg));
    n = recv(sig_fd, &msg, sizeof(msg) - 1, 0);
    if (n > CR_LOG_SIGNAL_PREFIX_LEN && !memcmp(msg.str, CR_LOG_SIGNAL_PREFIX, CR_LOG_SIGNAL_PREFIX_LEN)) {
      cr_log("debug", "payload", "%s", msg.str + CR_LOG_SIGNAL_PREFIX_LEN);
      continue;
    }
    break;
  }
  cr_log("debug", "hotkey_hook", "payload answer: %zd byte(s) after %llu ms", n,
         (unsigned long long)(now_ms() - wait_start));

  if (n == (ssize_t)sizeof(cr_hook_request_t) && msg.req.magic == CR_HOOK_REQUEST_MAGIC) {
    /* The payload may already have armed sites; mark the shell before anything can fail. */
    write_ready_pid(pid);
    return arm_hook_sites(pid, &msg.req);
  }

  char *buf = msg.str;
  if (n > 0 && !strcmp(buf, "old")) {
    /* Hooks from an earlier session stay live and answer the config broadcast, so keep them. */
    cr_log("info", "hotkey_hook", "SceShellUI (pid=%d) still has hooks from a previous session - using them", (int)pid);
    write_ready_pid(pid);
    return 0;
  }
  if (n > 0 && !strcmp(buf, "err")) {
    cr_log("warn", "hotkey_hook", "hook setup failed inside SceShellUI (pid=%d) - "
                                   "the payload lines above have the reason", (int)pid);
    return CR_INJECT_PAYLOAD_FAILED;
  }

  cr_log("warn", "hotkey_hook", "no answer from SceShellUI (pid=%d) - Mono probably "
                                 "isn't loaded yet", (int)pid);
  return CR_INJECT_PAYLOAD_FAILED;
}
#endif

void *
hotkey_hook_thread(void *arg) {
  (void)arg;
  syscall(SYS_thr_set_name, -1, "CheatRunner.elf");

#ifdef __SCE__
  while (g_hotkey_hook_running && !cr_priv_can_patch_game_memory()) {
    usleep(500 * 1000);
  }
  if (!g_hotkey_hook_running) {
    return NULL;
  }

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    cr_log("warn", "hotkey_hook", "couldn't open the signal socket");
    return NULL;
  }

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  /* SO_REUSEPORT lets a new instance bind immediately even if a SIGKILL'd old process is still alive in kernel sleep. */
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CR_HOTKEY_SIGNAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    cr_log("warn", "hotkey_hook", "couldn't bind the signal port");
    close(fd);
    return NULL;
  }

  /* recv() times out periodically so this loop can both notice shutdown
   * and re-check the hook on a timer. */
  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  pthread_t watcher;
  if (pthread_create(&watcher, NULL, shellui_exec_watch, NULL) == 0) {
    pthread_detach(watcher);
  }

  pid_t last_hooked_pid = -1;
  int seen_exec_seq = 0;
  pid_t failed_pid = -1;
  int shell_losses = 0;
  int reinject_count = 0;
  int attach_fail_count = 0;
  int total_fail_count = 0;
  int payload_fail_count = 0;
  int gave_up = 0;
  int restart_loop = 0;
  pid_t attempt_pid = -1;
  uint64_t last_hook_ms = 0;
  int was_sleeping = 0;
  uint64_t next_check_ms = now_ms();

  char buf[CR_LOG_SIGNAL_MAX + CR_LOG_SIGNAL_PREFIX_LEN + 1];
  while (g_hotkey_hook_running) {
    uint64_t now = now_ms();

    pthread_mutex_lock(&g_cfg_lock);
    int hotkey_enabled = g_cfg.hotkey_enabled;
    pthread_mutex_unlock(&g_cfg_lock);

    if (!hotkey_enabled) {
      /* Master off switch: never attach to SceShellUI (some firmwares crash from the hook).
       * Polls cheaply so re-enabling needs no restart. */
      restart_loop = 0;
      shell_losses = 0;
      next_check_ms = now + 5000;
    } else if (!restart_loop && now >= next_check_ms) {
      int exec_seq = __atomic_load_n(&g_shellui_exec_seq, __ATOMIC_ACQUIRE);
      if (exec_seq != seen_exec_seq) {
        /* New SceShellUI image: forget what we knew about the old one. */
        seen_exec_seq = exec_seq;
        unlink(CR_HOTKEY_READY_FILE);
        last_hooked_pid = -1;
        attempt_pid = -1;
        g_settle_pid = -1;
      }
      pid_t pid = find_pid_by_name("SceShellUI");
      if (pid != attempt_pid) {
        /* A new SceShellUI process gets a clean slate, e.g. after rest mode. */
        attempt_pid = pid;
        attach_fail_count = 0;
        total_fail_count = 0;
        payload_fail_count = 0;
        gave_up = 0;
        cr_log("debug", "hotkey_hook", "SceShellUI is pid=%d (hooked=%d, ready file=%d)", (int)pid,
               (int)last_hooked_pid, (int)read_ready_pid());
        if (failed_pid > 0 && pid > 0 && failed_pid != pid) {
          /* The shell went away after an injection that never answered. */
          failed_pid = -1;
          if (++shell_losses >= CR_HOTKEY_MAX_SHELL_LOSSES) {
            restart_loop = 1;
            gave_up = 1;
            cr_log("warn", "hotkey_hook", "SceShellUI restarted %d times right after we hooked it - leaving it alone "
                                           "until the shortcut is switched off and on", shell_losses);
            notify_hotkey_off();
          }
        }
      }
      int sleeping = console_sleeping();
      if (sleeping != was_sleeping) {
        cr_log("info", "hotkey_hook", "console %s", sleeping ? "is going to sleep, not touching SceShellUI" : "is awake again");
        was_sleeping = sleeping;
      }
      if (sleeping || gave_up) {
        next_check_ms = now + CR_HOTKEY_POLL_MS;
      } else if (pid <= 0) {
        next_check_ms = now + 5000;
      } else if (pid == last_hooked_pid) {
        /* same process we already hooked — nothing to do until it restarts. */
        next_check_ms = now + CR_HOTKEY_POLL_MS;
      } else if (read_ready_pid() == pid) {
        /* A previous CheatRunner already armed this shell; injecting again would find its hooks. */
        cr_log("info", "hotkey_hook", "SceShellUI (pid=%d) already has the hooks armed", (int)pid);
        last_hooked_pid = pid;
        broadcast_hotkey_config();
        next_check_ms = now + CR_HOTKEY_POLL_MS;
      } else if (!shellui_ready(pid) || !shellui_settled(pid, now)) {
        next_check_ms = now + 1000;
      } else {
        int inject_rc = inject_hook(pid, fd);
        /* inject_hook raised SO_RCVTIMEO to wait for the payload; put it back to the 1 s poll. */
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (inject_rc == 0) {
          last_hooked_pid = pid;
          attach_fail_count = 0;
          total_fail_count = 0;
          shell_losses = 0;
          broadcast_hotkey_config();
          next_check_ms = now + CR_HOTKEY_POLL_MS;
          reinject_count = (last_hook_ms && now - last_hook_ms < CR_HOTKEY_RESTART_WINDOW_MS) ? reinject_count + 1 : 1;
          last_hook_ms = now;
          if (reinject_count > CR_HOTKEY_MAX_REINJECTS) {
            restart_loop = 1;
            cr_log("warn", "hotkey_hook",
                   "SceShellUI restarted %d times in a row — giving up on the hotkey "
                   "until it is switched off and on", reinject_count);
            notify_hotkey_off();
          }
        } else if (inject_rc == CR_INJECT_PAYLOAD_FAILED &&
                   (failed_pid = pid, ++payload_fail_count >= CR_HOTKEY_MAX_PAYLOAD_FAILS)) {
          gave_up = 1;
          cr_log("warn", "hotkey_hook",
                 "the payload failed %d times in SceShellUI - giving up on this SceShellUI "
                 "until it restarts", payload_fail_count);
          notify_hotkey_off();
        } else {
          attach_fail_count++;
          total_fail_count++;
          int di = total_fail_count - 1;
          int dn = (int)(sizeof(k_retry_delay_ms) / sizeof(k_retry_delay_ms[0]));
          next_check_ms = now + k_retry_delay_ms[di < dn ? di : dn - 1];
          if (total_fail_count >= CR_HOTKEY_MAX_TOTAL_FAILS) {
            gave_up = 1;
            cr_log("warn", "hotkey_hook",
                   "injecting into SceShellUI failed %d times — giving up on this "
                   "SceShellUI until it restarts", total_fail_count);
            notify_hotkey_off();
          } else if (attach_fail_count >= CR_HOTKEY_MAX_ATTACH_FAILS) {
            /* No restart here: killing a wedged SceShellUI can stop it respawning at all. Back off instead. */
            cr_log("warn", "hotkey_hook",
                   "SceShellUI (pid=%d) failed to inject %d times in a row - "
                   "backing off, leaving it alone", (int)pid, attach_fail_count);
            attach_fail_count = 0;
            next_check_ms = now + CR_HOTKEY_BACKOFF_MS;
          }
        }
      }
    }

    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
      /* Timeout: also the ~1 s tick to re-push the config in case it changed (e.g. from the dashboard). */
      if (last_hooked_pid > 0) {
        broadcast_hotkey_config();
      }
      continue;
    }
    buf[n] = 0;

    if (n > CR_LOG_SIGNAL_PREFIX_LEN && !memcmp(buf, CR_LOG_SIGNAL_PREFIX, CR_LOG_SIGNAL_PREFIX_LEN)) {
      cr_log("debug", "payload", "%s", buf + CR_LOG_SIGNAL_PREFIX_LEN);
      continue;
    }

    /* A late hook request (payload retried after we stopped waiting) would look like a button press. */
    if (n >= (ssize_t)sizeof(uint32_t) &&
        !memcmp(buf, &(uint32_t){CR_HOOK_REQUEST_MAGIC}, sizeof(uint32_t))) {
      pid_t shell = find_pid_by_name("SceShellUI");
      if (n == (ssize_t)sizeof(cr_hook_request_t) && shell > 0 && shell != last_hooked_pid) {
        cr_hook_request_t late;
        memcpy(&late, buf, sizeof(late));
        write_ready_pid(shell);
        if (arm_hook_sites(shell, &late) == 0) {
          cr_log("info", "hotkey_hook", "late hook request taken for SceShellUI (pid=%d)", (int)shell);
          last_hooked_pid = shell;
          broadcast_hotkey_config();
        }
      }
      continue;
    }

    pthread_mutex_lock(&g_cfg_lock);
    int port = g_cfg.http_port;
    pthread_mutex_unlock(&g_cfg_lock);

    char url[128];

    if (n > CR_MENU_SIGNAL_PREFIX_LEN &&
        !memcmp(buf, CR_MENU_SIGNAL_PREFIX, CR_MENU_SIGNAL_PREFIX_LEN)) {
      /* OptionsMenu row clicked: open that game trainer, even if it is not the one running. */
      snprintf(url, sizeof(url), "http://127.0.0.1:%d/#trainer=%s", port,
               buf + CR_MENU_SIGNAL_PREFIX_LEN);
      cr_log("info", "hotkey_hook", "OptionsMenu entry clicked — opening %s", url);
      cr_browser_open_url(url);
      continue;
    }

    /* A game is running, so the hotkey was pressed in-game: open its trainer via the #trainer= hash. */
    running_game_state_t rgs;
    running_state_get(&rgs);

    if (rgs.running && rgs.title_id[0]) {
      snprintf(url, sizeof(url), "http://127.0.0.1:%d/#trainer=%s", port, rgs.title_id);
    } else {
      snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);
    }
    cr_log("info", "hotkey_hook", "hotkey held — opening %s", url);
    cr_browser_open_url(url);
  }

  close(fd);
#endif /* __SCE__ */

  return NULL;
}
