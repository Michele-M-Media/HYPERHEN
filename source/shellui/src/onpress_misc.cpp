/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress misc (kstuff, account, credits) */
#include "onpress.hpp"
#include "account_activator.h"
#include "progress_dialog.hpp"
#include "onion_cjson.hpp"
#include "debug_tools.hpp"

#include <onion/payload_identity.h>

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

struct DebugToolLaunch {
  const onion::shellui::debug_tools::Tool *tool;
  std::string key;
};

struct DebugToolStop {
  const onion::shellui::debug_tools::Tool *tool;
  pid_t pid;
  std::string pid_path;
};

bool request_debug_tool_shutdown(const onion::shellui::debug_tools::Tool &tool) {
  if (tool.stop_mode != onion::shellui::debug_tools::StopMode::GracefulHttp)
    return false;

  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;
  const timeval timeout{1, 0};
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(tool.port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) !=
      0) {
    close(fd);
    return false;
  }
  constexpr char request[] =
      "GET /api/shutdown HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  size_t sent_total = 0;
  while (sent_total < sizeof(request) - 1) {
    const ssize_t sent =
        send(fd, request + sent_total, sizeof(request) - 1 - sent_total,
             MSG_NOSIGNAL);
    if (sent <= 0)
      break;
    sent_total += static_cast<size_t>(sent);
  }
  const bool sent = sent_total == sizeof(request) - 1;
  if (sent) {
    char response[128];
    (void)recv(fd, response, sizeof(response), 0);
  }
  close(fd);
  return sent;
}

void *stop_debug_tool_thread(void *opaque) {
  auto *stop = static_cast<DebugToolStop *>(opaque);
  const auto *tool = stop->tool;
  const bool graceful = request_debug_tool_shutdown(*tool);
  for (int waited = 0; waited < 8000; waited += 100) {
    if (onion::shellui::debug_tools::running_pid(*tool) <= 1) {
      if (!stop->pid_path.empty())
        unlink(stop->pid_path.c_str());
      notify("notify.process.killed", tool->display_name);
      delete stop;
      return nullptr;
    }
    usleep(100000);
  }
  LOG_WARN("Debug service %s did not stop%s; using fallback termination",
           tool->display_name, graceful ? " after graceful shutdown" : "");
  const pid_t current = onion::shellui::debug_tools::running_pid(*tool);
  if (current == stop->pid)
    IPC_Client::getInstance(false).ForceKillPID(static_cast<int>(current));
  else
    LOG_WARN("Debug service %s changed PID while stopping; fallback skipped",
             tool->display_name);
  for (int waited = 0; waited < 1000; waited += 100) {
    if (onion::shellui::debug_tools::running_pid(*tool) <= 1)
      break;
    usleep(100000);
  }
  if (onion::shellui::debug_tools::running_pid(*tool) <= 1 &&
      !stop->pid_path.empty())
    unlink(stop->pid_path.c_str());
  notify("notify.process.killed", tool->display_name);
  delete stop;
  return nullptr;
}

void *launch_debug_tool_thread(void *opaque) {
  auto *launch = static_cast<DebugToolLaunch *>(opaque);
  const auto *tool = launch->tool;
  notify("notify.payload.loading", tool->path);
  if (IPC_Client::getInstance(true).LaunchPayload(tool->path, launch->key) !=
      IPC_Ret::NO_ERROR) {
    notify("notify.payload.launch_failed", tool->path, launch->key.c_str());
  }
  delete launch;
  return nullptr;
}

OnPressResult toggle_debug_tool(OnPressContext &ctx) {
  ctx.dirty = false;
  const auto *tool = onion::shellui::debug_tools::find(ctx.id);
  if (!tool)
    return OnPressResult::NotMine;

  char key[ONION_PAYLOAD_IDENTITY_SIZE] = {};
  if (!onion::shellui::debug_tools::identity(*tool, key, sizeof(key))) {
    notify("notify.payload.launch_failed", tool->path, "invalid_identity");
    return OnPressResult::Consumed;
  }

  char pid_path[256] = {};
  const pid_t pid = onion::shellui::debug_tools::running_pid(
      *tool, pid_path, sizeof(pid_path));
  if (!value_as_int(ctx)) {
    /* One-shot tools patch state and normally exit immediately. Killing them
     * midway through that operation is less safe than waiting for completion;
     * their applied state is reset by a console reboot. */
    if (tool->stop_mode == onion::shellui::debug_tools::StopMode::OneShot)
      return OnPressResult::Consumed;
    if (pid > 1) {
      if (tool->stop_mode == onion::shellui::debug_tools::StopMode::GracefulHttp) {
        auto *stop = new DebugToolStop{tool, pid, pid_path};
        pthread_t thread;
        if (pthread_create(&thread, nullptr, stop_debug_tool_thread, stop) == 0) {
          pthread_detach(thread);
        } else {
          delete stop;
          IPC_Client::getInstance(false).ForceKillPID(static_cast<int>(pid));
          unlink(pid_path);
          notify("notify.process.killed", tool->display_name);
        }
      } else {
        IPC_Client::getInstance(false).ForceKillPID(static_cast<int>(pid));
        unlink(pid_path);
        notify("notify.process.killed", tool->display_name);
      }
    }
    return OnPressResult::Consumed;
  }

  if (pid > 1)
    return OnPressResult::Consumed;

  if (tool->port != 0) {
    size_t tool_count = 0;
    const auto *tools = onion::shellui::debug_tools::all(&tool_count);
    for (size_t i = 0; i < tool_count; ++i) {
      if (&tools[i] == tool || tools[i].port != tool->port)
        continue;
      if (onion::shellui::debug_tools::running_pid(tools[i]) > 1) {
        LOG_WARN("Debug service %s conflicts with active %s on port %u",
                 tool->display_name, tools[i].display_name, tool->port);
        notify("notify.payload.launch_failed", tool->path, "port_in_use");
        return OnPressResult::Consumed;
      }
    }
  }

  if (!onion::shellui::debug_tools::supported_on_current_firmware(*tool)) {
    notify("notify.payload.launch_failed", tool->path, "unsupported_profile");
    return OnPressResult::Consumed;
  }

  auto *launch = new DebugToolLaunch{tool, key};
  pthread_t thread;
  if (pthread_create(&thread, nullptr, launch_debug_tool_thread, launch) == 0) {
    pthread_detach(thread);
  } else {
    delete launch;
    notify("notify.payload.launch_failed", tool->path, key);
  }
  return OnPressResult::Consumed;
}

} // namespace

OnPressResult onpress_kstuff_autoload(OnPressContext &ctx) {
  const bool enabled = value_as_int(ctx);
  if (enabled == g_settings.kstuff_autoload)
    return OnPressResult::EarlyReturn;

  g_settings.kstuff_autoload = enabled;
  if (enabled) {
    unlink("/user/data/HyperHEN/no_kstuff");
    unlink("/data/HyperHEN/no_kstuff");
    notify("notify.kstuff.next_boot_on");
  } else {
    touch_file("/user/data/HyperHEN/no_kstuff");
    touch_file("/data/HyperHEN/no_kstuff");
    notify("notify.kstuff.next_boot_off");
  }
  return OnPressResult::Handled;
}

static OnPressResult id_download_cheats(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string reply;
  if (!IPC_Client::getInstance(true).DownloadCheats(nullptr, nullptr, reply)) {
    notify("notify.cheats.sync.error", "ipc");
    return OnPressResult::Consumed;
  }
  onion_cjson::Root response(reply);
  const char *state = response && cJSON_IsObject(response.get())
                          ? onion_cjson::string_item(response.get(), "state")
                          : nullptr;
  if (!state) {
    notify("notify.cheats.sync.error", "ipc_response");
    return OnPressResult::Consumed;
  }
  const int task_id = onion_cjson::int_item(response.get(), "task_id", 0);
  if (std::strcmp(state, "already_running") == 0) {
    notify("notify.cheats.sync.busy");
  }
  /* Button confirmation has completed before this handler runs. */
  cheat_progress_show(task_id > 0 ? static_cast<uint32_t>(task_id) : 0);
  if (!cheat_progress_open_page()) {
    notify("notify.cheats.sync.error", "navigation");
  }
  return OnPressResult::Consumed;
}

static OnPressResult id_activate_account(OnPressContext &ctx) {
  ctx.dirty = false;
  Activator activator(true);
  if (!activator.Valid()) {
    notify("notify.account.invalid");
    return OnPressResult::Consumed;
  }
  if (!activator.IsNotActivated()) {
    notify("notify.account.already");
    return OnPressResult::Consumed;
  }
  if (!activator.Activate()) {
    notify("notify.account.failed");
    return OnPressResult::Consumed;
  }
  notify("notify.account.activated");
  return OnPressResult::Consumed;
}

static OnPressResult id_lm_test(OnPressContext &ctx) {
  (void)ctx;
  LOG_DEBUG("LM's Test Button Pressed");
  return OnPressResult::Handled;
}

static OnPressResult id_hyperhen_credits(OnPressContext &ctx) {
  (void)ctx;
  return OnPressResult::EarlyReturn;
}

static const OnPressExactEntry kRootExact[] = {
    {"id_download_cheats", id_download_cheats},
    {"id_lm_test", id_lm_test},
    {"id_hyperhen_credits", id_hyperhen_credits},
    {"id_debug_service_klog", toggle_debug_tool},
    {"id_debug_service_ftp", toggle_debug_tool},
    {"id_debug_service_elfldr", toggle_debug_tool},
    {"id_debug_service_fw_spoof", toggle_debug_tool},
    {"id_debug_service_np_fake_signin", toggle_debug_tool},
    {"id_debug_service_nanodns", toggle_debug_tool},
    {"id_debug_service_chukei", toggle_debug_tool},
    {"id_debug_service_zftpd", toggle_debug_tool},
    {"id_debug_service_garlic", toggle_debug_tool},
};

const OnPressExactEntry *onpress_misc_root_exact(size_t *count) {
  *count = sizeof(kRootExact) / sizeof(kRootExact[0]);
  return kRootExact;
}

static const OnPressExactEntry kAccountExact[] = {
    {"id_activate_account", id_activate_account},
};

const OnPressExactEntry *onpress_account_exact(size_t *count) {
  *count = sizeof(kAccountExact) / sizeof(kAccountExact[0]);
  return kAccountExact;
}
