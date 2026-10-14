/* Copyright (C) 2026 HyperHEN */
#include "debug_tools.hpp"

#include "shellui_payload_state.hpp"

#include <onion/payload_identity.h>

namespace onion::shellui::debug_tools {
namespace {

constexpr Tool kTools[] = {
    {"id_debug_service_klog", "/data/HyperHEN/tools/klogsrv.elf",
     "Kernel Log Server", "debug.klog", "debug.klog.sub", 3232},
    {"id_debug_service_ftp", "/data/HyperHEN/tools/ftpsrv.elf",
     "FTP Server", "debug.ftp", "debug.ftp.sub", 2121},
    {"id_debug_service_elfldr",
     "/data/HyperHEN/tools/hyperhen_debug_elfldr.elf", "ELF Loader",
     "debug.elfldr", "debug.elfldr.sub", 9025},
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

} // namespace onion::shellui::debug_tools
