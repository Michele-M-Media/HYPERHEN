/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <string>
#include <string_view>

namespace hyperdeck::engines {

struct LaunchResult {
  int status = 200;
  bool ready = false;
  std::string message;
  std::string url;
};

std::string snapshot_json(std::string_view token);
LaunchResult start(std::string_view name);
void reap();
void shutdown();

} // namespace hyperdeck::engines
