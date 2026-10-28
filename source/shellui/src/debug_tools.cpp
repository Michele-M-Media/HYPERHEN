/* Copyright (C) 2026 HyperHEN */
#include "debug_tools.hpp"

#include "external_symbols.hpp"
#include "shellui_payload_state.hpp"

#include <onion/payload_identity.h>

#include "fw_offsets_guard.h"

namespace onion::shellui::debug_tools {
namespace {

constexpr Tool kTools[] = {
    {"id_debug_service_klog", "/data/HyperHEN/tools/klogsrv.elf",
     "Kernel Log Server", "debug.klog", "debug.klog.sub", 3232,
     StopMode::ForceKill, false},
    {"id_debug_service_ftp", "/data/HyperHEN/tools/ftpsrv.elf",
     "FTP Server", "debug.ftp", "debug.ftp.sub", 2121,
     StopMode::ForceKill, false},
    {"id_debug_service_elfldr",
     "/data/HyperHEN/tools/hyperhen_debug_elfldr.elf", "ELF Loader",
     "debug.elfldr", "debug.elfldr.sub", 9025, StopMode::ForceKill, false},
    {"id_debug_service_fw_spoof",
     "/data/HyperHEN/tools/ps5-fw-spoof.elf", "PS5 Firmware Spoof",
     "debug.fw_spoof", "debug.fw_spoof.sub", 0, StopMode::OneShot, false},
    {"id_debug_service_np_fake_signin",
     "/data/HyperHEN/tools/np-fake-signin-ps5.elf", "NP Fake Sign-in",
     "debug.np_fake_signin", "debug.np_fake_signin.sub", 0,
     StopMode::OneShot, false},
    {"id_debug_service_nanodns", "/data/HyperHEN/tools/nanodns.elf",
     "NanoDNS", "debug.nanodns", "debug.nanodns.sub", 53,
     StopMode::ForceKill, false},
    {"id_debug_service_chukei", "/data/HyperHEN/tools/chukei-ps5.elf",
     "Chukei DNS", "debug.chukei", "debug.chukei.sub", 53,
     StopMode::ForceKill, false},
    {"id_debug_service_zftpd",
     "/data/HyperHEN/tools/zftpd-ps5-zhttp-v1.6.0.elf", "zFTPd + zHTTP",
     "debug.zftpd", "debug.zftpd.sub", 0, StopMode::ForceKill, false},
    {"id_debug_service_garlic",
     "/data/HyperHEN/tools/garlic-savemgr.elf", "Garlic Save Manager",
     "debug.garlic", "debug.garlic.sub", 8082, StopMode::GracefulHttp,
     true},
};

} // namespace

const Tool *all(size_t *count) {
  if (count)
    *count = sizeof(kTools) / sizeof(kTools[0]);
  return kTools;
}

const Tool *find(std::string_view control_id) {
  for (const auto &tool : kTools)
    if (control_id == tool.control_id)
      return &tool;
  return nullptr;
}

bool identity(const Tool &tool, char *out, size_t out_size) {
  return out && out_size > 0 &&
         onion_payload_identity_from_path(tool.path, out, out_size);
}

pid_t running_pid(const Tool &tool, char *pid_path, size_t pid_path_size) {
  char key[ONION_PAYLOAD_IDENTITY_SIZE] = {};
  if (!identity(tool, key, sizeof(key)))
    return -1;
  return shellui_payload_resolve_recorded_pid(key, pid_path, pid_path_size);
}

bool supported_on_current_firmware(const Tool &tool) {
  if (!tool.exact_firmware_profiles)
    return true;
  OrbisKernelSwVersion sw{};
  return sceKernelGetProsperoSystemSwVersion &&
         sceKernelGetProsperoSystemSwVersion(&sw) == 0 &&
         garlic_fw_find(sw.version) != nullptr;
}

} // namespace onion::shellui::debug_tools
