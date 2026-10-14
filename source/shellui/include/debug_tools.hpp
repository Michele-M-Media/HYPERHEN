/* Copyright (C) 2026 HyperHEN
 * Embedded development-service registry used by the Debug toolbox group.
 */
#pragma once

#include <cstddef>
#include <string_view>
#include <sys/types.h>

namespace onion::shellui::debug_tools {

struct Tool {
  const char *control_id;
  const char *path;
  const char *display_name;
  const char *title_key;
  const char *description_key;
  unsigned short port;
};

const Tool *all(size_t *count);
const Tool *find(std::string_view control_id);
bool identity(const Tool &tool, char *out, size_t out_size);
pid_t running_pid(const Tool &tool, char *pid_path = nullptr,
                  size_t pid_path_size = 0);

} // namespace onion::shellui::debug_tools
