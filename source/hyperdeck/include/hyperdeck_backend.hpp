/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "hyperdeck_model.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hyperdeck {
struct Result { int status = 200; std::string message; };
struct Entry { std::string id, name; bool running = false; };
uint64_t monotonic_ms();
Snapshot observe();
Result apply(std::string_view action, bool enabled, std::string_view id);
std::vector<Entry> inventory(std::string_view kind);
bool install_media_launcher();
void report_http_ready();
void report_startup_failure(const char *stage, int error);
void clear_http_ready();
bool backend_initialize(bool selector_mode);
uint32_t backend_firmware();
void backend_shutdown();
std::string json_quote(std::string_view value);
}
