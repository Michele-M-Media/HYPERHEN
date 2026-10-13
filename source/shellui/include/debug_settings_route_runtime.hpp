#pragma once

#include <cstdint>
#include <string>

void shellui_configure_debug_settings_route(uint32_t system_version);
bool shellui_debug_settings_uses_old_route(void);
const char *shellui_debug_settings_toolbox_uri(void);
const char *shellui_debug_settings_toolbox_uri_simple(void);
std::string shellui_rewrite_debug_settings_route(const std::string &uri);
