/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress network domain */
#include "account_activator.h"
#include "onpress.hpp"
#include "remote_play.hpp"

#include <cstdlib>
#include <fstream>

static OnPressResult id_disp_titleids(OnPressContext &ctx) {
  bool &dis_tids = g_settings.display_tids;
  if (atol(ctx.value.c_str()) == dis_tids) {
    LOG_WARN("Display TIDs already %s", dis_tids ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  dis_tids = !dis_tids;
  ReloadRNPSApp("NPXS40002");
  return OnPressResult::Handled;
}

static OnPressResult id_remote_play(OnPressContext &ctx) {
  ctx.dirty = false;
  Activator activator(true);
  if (!activator.Valid()) {
    notify("notify.account.invalid");
    return OnPressResult::Consumed;
  }
  if (activator.IsNotActivated())
    return OnPressResult::Consumed;
  return OnPressResult::NotMine;
}

static OnPressResult id_save_rp_info(OnPressContext &) {
  const int usb = usbpath();
  if (usb < 0 || g_remote_play_info.empty()) {
    notify("notify.remote_play.details_unavailable");
    return OnPressResult::EarlyReturn;
  }

  const std::string path = "/usb" + std::to_string(usb) +
                           "/remote_play_info.txt";
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    notify("notify.remote_play.save_failed");
    return OnPressResult::EarlyReturn;
  }
  output << g_remote_play_info << '\n';
  notify("notify.remote_play.saved");
  return OnPressResult::Consumed;
}

static const OnPressExactEntry kExact[] = {
    {"id_disp_titleids", id_disp_titleids},
    {"id_remote_play", id_remote_play},
    {"id_save_rp_info", id_save_rp_info},
};

const OnPressExactEntry *onpress_network_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
