/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "startup_navigation.hpp"

#include <onion/platform.h>
#include <onion/settings.hpp>
#include <ps5/kernel.h>

#include <cstddef>
#include <cstdint>

extern "C" {
int sceKernelLoadStartModule(const char *name, size_t argc, const void *argv,
                             uint32_t flags, void *unknown, int *result);
int sceUserServiceGetForegroundUser(int *user_id);
}

namespace onion::daemon {
namespace {

struct ShellUIUtilLaunchByUriParam {
  unsigned int size;
  uint32_t user_id;
};

int navigate_home_menu_via_uri() {
  constexpr const char *kHomeMenuUri =
      "pshomeui:navigateToHome?bootCondition=psButton";
  using ShellUIUtilInitialize = int (*)(void);
  using ShellUIUtilLaunchByUri =
      int (*)(const char *, ShellUIUtilLaunchByUriParam *);

  const int module = sceKernelLoadStartModule(
      "/system_ex/common_ex/lib/libSceShellUIUtil.sprx", 0, nullptr, 0,
      nullptr, nullptr);
  if (module < 0) {
    LOG_WARN("Failed to load libSceShellUIUtil.sprx: 0x%x",
             static_cast<unsigned int>(module));
    return module;
  }

  const auto initialize = reinterpret_cast<ShellUIUtilInitialize>(
      kernel_dynlib_dlsym(-1, static_cast<uint32_t>(module),
                          "sceShellUIUtilInitialize"));
  const auto launch_by_uri = reinterpret_cast<ShellUIUtilLaunchByUri>(
      kernel_dynlib_dlsym(-1, static_cast<uint32_t>(module),
                          "sceShellUIUtilLaunchByUri"));
  if (!initialize || !launch_by_uri) {
    LOG_WARN("Failed to resolve ShellUIUtil URI entry points");
    return -1;
  }

  ShellUIUtilLaunchByUriParam param{};
  param.size = sizeof(param);
  initialize();
  (void)sceUserServiceGetForegroundUser(
      reinterpret_cast<int *>(&param.user_id));
  return launch_by_uri(kHomeMenuUri, &param);
}

} // namespace

void apply_startup_destination(const Settings &settings) {
  if (settings.startup_open_after_load == kStartupOpenNone) {
    LOG_DEBUG("Startup destination: none");
    return;
  }

  if (settings.startup_open_after_load != kStartupOpenHomeMenu) {
    LOG_WARN("Ignoring unsupported startup destination: %d",
             settings.startup_open_after_load);
    return;
  }

  const int result = navigate_home_menu_via_uri();
  if (result < 0) {
    LOG_WARN("Failed to open Home Menu after startup: 0x%x",
             static_cast<unsigned int>(result));
    return;
  }
  LOG_INFO("Opened Home Menu after startup");
}

} // namespace onion::daemon
