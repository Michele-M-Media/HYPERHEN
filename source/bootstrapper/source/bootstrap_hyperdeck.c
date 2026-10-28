/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootstrap_hyperdeck.h"
#include "bootstrap_notify.h"
#include <onion/boot_mode_5xx.h>
#include <onion/firmware_capabilities.h>
#include <onion/engine_selector.h>
#include <onion/log.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef HYPERHEN_HYPERDECK_MEDIA_LAUNCHER
#define HYPERHEN_HYPERDECK_MEDIA_LAUNCHER 1
#endif

__asm__(".pushsection .rodata\n"
        ".balign 16\n"
        ".global hyperdeck_websrv_start\n"
        "hyperdeck_websrv_start:\n"
        ".incbin \"" HYPERHEN_WEBDECK_ELF "\"\n"
        "hyperdeck_websrv_end:\n"
        ".balign 4\n"
        ".global hyperdeck_websrv_size\n"
        "hyperdeck_websrv_size:\n"
        ".int hyperdeck_websrv_end-hyperdeck_websrv_start\n"
        ".popsection\n");
extern const uint8_t hyperdeck_websrv_start[];
extern const unsigned int hyperdeck_websrv_size;

int sceSystemServiceLaunchWebBrowser(const char *uri, void *reserved);

#define HYPERHEN_5XX_SELECTOR_URL \
  "http://127.0.0.1:8088/selector-5xx-r10-20261025.html"
#define HYPERHEN_5XX_SELECTOR_TIMEOUT_MS 600000
#define HYPERHEN_5XX_SELECTOR_SETTLE_US 2500000U

static pid_t owned_listener(void) {
  pid_t pid=-1;
  if(onion_ready_read_pid(ONION_READY_HYPERDECK_LISTENER,&pid) && pid>1 &&
     onion_proc_is_alive(pid) && onion_find_pid("hyperdeck.elf")==pid)
    return pid;
  return -1;
}

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC,&now) != 0) return 0;
  return (uint64_t)now.tv_sec*1000U+(uint64_t)now.tv_nsec/1000000U;
}

static void selector_stop_owned(pid_t pid) {
  if (pid <= 1 || !onion_proc_is_alive(pid) ||
      onion_find_pid("hyperdeck.elf") != pid)
    return;

  if (kill(pid, SIGTERM) != 0 && errno != ESRCH)
    LOG_WARN("Cannot stop 5.xx selector pid=%d: %s", (int)pid,
             strerror(errno));
  for (int waited = 0; waited < 2000 && onion_proc_is_alive(pid);
       waited += 100)
    usleep(100000);
  if (onion_proc_is_alive(pid) && onion_find_pid("hyperdeck.elf") == pid) {
    LOG_WARN("5.xx selector pid=%d ignored SIGTERM; stopping owned process",
             (int)pid);
    (void)kill(pid, SIGKILL);
  }
}

static void selector_cleanup(pid_t pid, bool stop_process,
                             bool clear_active_marker) {
  if (stop_process)
    selector_stop_owned(pid);
  if (clear_active_marker &&
      unlink(ONION_5XX_SELECTOR_ACTIVE_PATH) != 0 && errno != ENOENT)
    LOG_WARN("Cannot remove 5.xx selector marker: %s", strerror(errno));
  onion_ready_clear(ONION_READY_HYPERDECK);
  onion_ready_clear(ONION_READY_HYPERDECK_LISTENER);
  onion_ready_clear(ONION_READY_HYPERDECK_FAILED);
}

static bool selector_publish_active(void) {
  (void)mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  int fd = open(ONION_5XX_SELECTOR_ACTIVE_PATH,
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (fd < 0)
    return false;
  const char active = '1';
  const bool written = write(fd, &active, 1) == 1 && fsync(fd) == 0;
  const int saved_errno = errno;
  (void)close(fd);
  errno = saved_errno;
  if (!written)
    (void)unlink(ONION_5XX_SELECTOR_ACTIVE_PATH);
  return written;
}

Onion5xxBootMode bootstrap_hyperdeck_select_5xx_mode(uint32_t firmware) {
  if (!onion_engine_selector_supported(firmware))
    return ONION_5XX_BOOT_MODE_DEFAULT;

  Onion5xxBootMode selected =
      onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
  if (onion_engine_selector_accepts(firmware, selected)) {
    LOG_INFO("Reusing engine per-boot selection: %s",
             onion_5xx_boot_mode_name(selected));
    /* Do not clear either the normal HyperDeck readiness markers or the
       selector-active marker here.  The latter remains the hand-off to the
       daemon until the user manually leaves the selector WebKit scene. */
    return selected;
  }
  if (access(ONION_5XX_BOOT_MODE_PATH, F_OK) == 0) {
    LOG_ERROR("Invalid 5.xx per-boot selection file; refusing fallback");
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "invalid_restart"));
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  if (access(ONION_5XX_SELECTOR_ACTIVE_PATH, F_OK) == 0) {
    const pid_t stale_pid = owned_listener();
    if (stale_pid > 1) {
      LOG_ERROR("A 5.xx selector is already active as pid=%d",
                (int)stale_pid);
      bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "already_active"));
      return ONION_5XX_BOOT_MODE_INVALID;
    }
    (void)unlink(ONION_5XX_SELECTOR_ACTIVE_PATH);
  }

  const pid_t unexpected = onion_find_pid("hyperdeck.elf");
  if (unexpected > 1 && onion_proc_is_alive(unexpected)) {
    LOG_ERROR("Cannot start 5.xx selector while HyperDeck pid=%d is active",
              (int)unexpected);
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "restart_required"));
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  selector_cleanup(-1, false, true);
  if (!selector_publish_active()) {
    LOG_ERROR("Cannot publish 5.xx selector mode: %s", strerror(errno));
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "prepare_failed"));
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  bootstrap_hyperdeck_record("selector_spawn", 0);
  const pid_t pid =
      bootstrap_hyperdeck_spawn(hyperdeck_websrv_start, hyperdeck_websrv_size);
  if (pid <= 1) {
    selector_cleanup(-1, false, true);
    bootstrap_hyperdeck_record("selector_spawn_failed", (int)pid);
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "start_failed"));
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  bool http_ready = false;
  for (int waited = 0; waited < 20000; waited += 100) {
    if (!onion_proc_is_alive(pid))
      break;
    if (owned_listener() == pid && bootstrap_hyperdeck_http_probe_selector()) {
      http_ready = true;
      break;
    }
    pid_t failed_pid = -1;
    if (onion_ready_read_pid(ONION_READY_HYPERDECK_FAILED, &failed_pid) &&
        failed_pid == pid)
      break;
    usleep(100000);
  }
  if (!http_ready) {
    selector_cleanup(pid, true, true);
    bootstrap_hyperdeck_record("selector_http_failed", (int)pid);
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "page_unavailable"));
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  char selector_url[192];
  (void)snprintf(selector_url, sizeof(selector_url), "%s?boot=%llu-%d",
                 HYPERHEN_5XX_SELECTOR_URL,
                 (unsigned long long)monotonic_ms(), (int)getpid());
  const int browser_result =
      sceSystemServiceLaunchWebBrowser(selector_url, NULL);
  bootstrap_hyperdeck_record("selector_browser", browser_result);
  if (browser_result < 0) {
    selector_cleanup(pid, true, true);
    bootstrap_notify("notify.selector.browser_open_failed");
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  bootstrap_notify(onion_engine_selector_is_legacy(firmware)
                       ? "notify.selector.legacy_choose_engine"
                       : "notify.selector.choose_engine");
  for (int waited = 0; waited < HYPERHEN_5XX_SELECTOR_TIMEOUT_MS;
       waited += 100) {
    selected = onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
    if (onion_engine_selector_accepts(firmware, selected))
      break;
    if (access(ONION_5XX_BOOT_MODE_PATH, F_OK) == 0 ||
        !onion_proc_is_alive(pid))
      break;
    usleep(100000);
  }

  if (!onion_engine_selector_accepts(firmware, selected)) {
    selector_cleanup(pid, true, true);
    bootstrap_hyperdeck_record("selector_no_choice", (int)selected);
    bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "no_choice"));
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  /*
   * Keep both the system-owned WebKit scene and its loopback backend alive
   * after the choice.  The user leaves the browser manually.  The daemon then
   * clears the selector-active marker after the concrete SceNKWebProcess has
   * exited; the post-pipeline path below owns retiring this exact listener.
   */
  usleep(HYPERHEN_5XX_SELECTOR_SETTLE_US);
  bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "choice_close_browser"));
  bootstrap_hyperdeck_record("selector_choice", (int)selected);
  LOG_INFO("Engine selector R20 fw=0x%08x chose %s", firmware,
           onion_5xx_boot_mode_name(selected));
  return selected;
}

static void selector_retire_backend(bool known_selector) {
  const pid_t pid = owned_listener();
  if (pid <= 1)
    return;
  if (!known_selector && !bootstrap_hyperdeck_http_probe_selector())
    return;
  LOG_INFO("5.xx selector browser closed; retiring backend pid=%d", (int)pid);
  selector_cleanup(pid, true, false);
}

static bool selector_retire_legacy_backend_checked(void) {
  const pid_t pid = owned_listener();
  if (pid <= 1) {
    const pid_t observed = onion_find_pid("hyperdeck.elf");
    /* A process with no matching ownership marker is not ours to stop. */
    return observed <= 1 || !onion_proc_is_alive(observed);
  }
  if (!bootstrap_hyperdeck_http_probe_selector())
    return false;
  selector_stop_owned(pid);
  for (int waited = 0; waited < 2000 && onion_proc_is_alive(pid);
       waited += 100)
    usleep(100000);
  if (onion_proc_is_alive(pid)) {
    LOG_WARN("Legacy selector backend pid=%d did not stop; engine blocked",
             (int)pid);
    return false;
  }
  selector_cleanup(pid, false, false);
  return true;
}

bool bootstrap_hyperdeck_wait_legacy_browser(uint32_t firmware) {
  if (!onion_engine_selector_is_legacy(firmware))
    return true;
  const Onion5xxBootMode mode =
      onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
  if (!onion_engine_selector_accepts(firmware, mode))
    return false;

  /* PS5 HEN's own startup contract requires the browser to be closed first.
   * Observe the real WebProcess, not an app title shared with ShellUI.
   * This gate is bootstrap-owned, leaving the tested R19 daemon untouched.
   */
  int quiet_polls = 0;
  bool web_observed = false;
  bool backend_retired = false;
  for (int poll = 0; poll < 2400; ++poll) {
    const pid_t web_pid = onion_find_pid("SceNKWebProcess");
    if (web_pid > 1) {
      web_observed = true;
      quiet_polls = 0;
    } else {
      ++quiet_polls;
      if (quiet_polls >= 8) {
        if (!backend_retired) {
          if (!selector_retire_legacy_backend_checked()) {
            bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "restart_required"));
            return false;
          }
          backend_retired = true;
          quiet_polls = 0;
          /* Retirement may have waited for process termination. Establish
           * a new quiet window after it, rather than accepting a browser
           * that reopened while the backend was being stopped.
           */
          continue;
        }
        if (unlink(ONION_5XX_SELECTOR_ACTIVE_PATH) != 0 && errno != ENOENT) {
          bootstrap_notify(ONION_ENGINE_SELECTOR_KEY(firmware, "prepare_failed"));
          return false;
        }
        LOG_INFO("Legacy selector browser closed (observed=%d); engine may start",
                 web_observed ? 1 : 0);
        return true;
      }
    }
    usleep(250000);
  }
  bootstrap_notify("notify.selector.legacy_close_browser");
  LOG_WARN("Legacy selector browser did not close; engine startup cancelled");
  return false;
}

static int selector_wait_browser_handoff(uint32_t firmware) {
  if (!onion_engine_selector_supported(firmware))
    return 1;

  bool marker_observed = access(ONION_5XX_SELECTOR_ACTIVE_PATH, F_OK) == 0;
  if (!marker_observed) {
    const Onion5xxBootMode mode =
        onion_5xx_boot_mode_read(ONION_5XX_BOOT_MODE_PATH);
    if (onion_engine_selector_accepts(firmware, mode))
      selector_retire_backend(false);
    return 1;
  }

  for (int waited = 0; waited < HYPERHEN_5XX_SELECTOR_TIMEOUT_MS;
       waited += 250) {
    if (access(ONION_5XX_SELECTOR_ACTIVE_PATH, F_OK) != 0) {
      selector_retire_backend(true);
      return 1;
    }
    usleep(250000);
  }

  LOG_WARN("5.xx selector browser did not close before handoff timeout");
  return 0;
}

static int firmware_pipeline_finished(uint32_t firmware) {
  const uint32_t capabilities =
      onion_firmware_capabilities(onion_firmware_route(firmware));
  if ((capabilities & ONION_FIRMWARE_CAP_TOOLBOX) == 0)
    return 1;
  if (!selector_wait_browser_handoff(firmware))
    return -1;

  /*
   * The daemon publishes its marker before the cold ShellUI injection ends.
   * Keep Hyperdeck outside that interval without changing the existing daemon
   * or Toolbox startup path.  A PID/name check rejects stale ready markers.
   */
  for (int waited = 0; waited < 90000; waited += 250) {
    pid_t ready_pid = -1;
    if (onion_ready_read_pid(ONION_READY_TOOLBOX, &ready_pid) &&
        ready_pid > 1 && onion_proc_is_alive(ready_pid) &&
        onion_find_pid("SceShellUI") == ready_pid)
      return 1;
    usleep(250000);
  }
  return 0;
}

void bootstrap_hyperdeck_start(uint32_t firmware) {
  if(onion_firmware_route(firmware)==ONION_FIRMWARE_ROUTE_UNSUPPORTED ||
     access("/data/HyperHEN/no_hyperdeck",F_OK)==0) {
    LOG_INFO("HyperDeck HTTP disabled");return;
  }
  bootstrap_hyperdeck_record("post_pipeline",0);
  const int pipeline_state=firmware_pipeline_finished(firmware);
  if(pipeline_state<0) {
    LOG_WARN("HyperDeck deferred because the 5.xx selector is still active");
    bootstrap_notify(onion_engine_selector_is_legacy(firmware)
                         ? "notify.selector.legacy_close_browser"
                         : "notify.hyperdeck.close_selector_browser");
    bootstrap_hyperdeck_record("selector_still_active",-1);
    return;
  }
  if(!pipeline_state) {
    /*
     * HyperDeck is a Media web app and owns no ShellUI hooks.  The Toolbox
     * marker is useful as a quiet-period barrier, but it must not permanently
     * suppress Media registration on a profile whose Dashboard injection is
     * unavailable.  The 90-second wait above still keeps AppInstUtil away
     * from the normal cold-injection window before this independent fallback.
     */
    LOG_WARN("HyperDeck continuing after the Toolbox quiet-period timeout");
    bootstrap_notify("notify.hyperdeck.toolbox_not_ready");
    bootstrap_hyperdeck_record("toolbox_not_ready",-1);
  }
  onion_ready_clear(ONION_READY_HYPERDECK);
  pid_t pid=owned_listener();
  if (pid<=1) {
    // An existing in-progress instance must not be duplicated or killed.
    pid=onion_find_pid("hyperdeck.elf");
    if(pid<=1 || !onion_proc_is_alive(pid)) {
      onion_ready_clear(ONION_READY_HYPERDECK_LISTENER);
      onion_ready_clear(ONION_READY_HYPERDECK_FAILED);
      bootstrap_hyperdeck_record("spawn_websrv",0);
      pid=bootstrap_hyperdeck_spawn(hyperdeck_websrv_start,hyperdeck_websrv_size);
      bootstrap_hyperdeck_record("spawn_result",(int)pid);
      if(pid<=1) {
        bootstrap_notify("notify.hyperdeck.server_start_failed");return;
      }
    }
  }
  bool http_ready=false;
  const uint64_t started=monotonic_ms();
  for(int waited=0;waited<20000;waited+=100) {
    if(!onion_proc_is_alive(pid)) {
      bootstrap_hyperdeck_record("websrv_exited",(int)pid);
      bootstrap_notify("notify.hyperdeck.server_exited");return;
    }
    if(owned_listener()==pid && bootstrap_hyperdeck_http_probe()) {
      http_ready=true;break;
    }
    pid_t failed_pid=-1;
    if(onion_ready_read_pid(ONION_READY_HYPERDECK_FAILED,&failed_pid) && failed_pid==pid) {
      bootstrap_hyperdeck_record("websrv_failed",(int)pid);
      bootstrap_notify("notify.hyperdeck.server_init_failed");return;
    }
    if(started && monotonic_ms()-started>=20000)break;
    usleep(100000);
  }
  if(!http_ready) {
    bootstrap_hyperdeck_record("http_probe_timeout",(int)pid);
    bootstrap_notify("notify.hyperdeck.server_unresponsive");return;
  }
  bootstrap_hyperdeck_record("http_verified",(int)pid);
#if HYPERHEN_HYPERDECK_MEDIA_LAUNCHER
  const int installed=bootstrap_hyperdeck_install_media();
  bootstrap_hyperdeck_record("media_registration",installed);
  if(!onion_proc_is_alive(pid) || !bootstrap_hyperdeck_http_probe()) {
    bootstrap_hyperdeck_record("http_lost_after_install",(int)pid);
    bootstrap_notify("notify.hyperdeck.installed_unresponsive");return;
  }
#endif
  if(!onion_ready_signal_pid(ONION_READY_HYPERDECK,pid)) {
    bootstrap_hyperdeck_record("ready_marker_failed",(int)pid);return;
  }
  bootstrap_hyperdeck_record("ready",(int)pid);
#if HYPERHEN_HYPERDECK_MEDIA_LAUNCHER
  bootstrap_notify(installed==0?"notify.hyperdeck.ready_media":"notify.hyperdeck.ready_media_deferred");
#else
  bootstrap_notify("notify.hyperdeck.background_ready");
#endif
}
