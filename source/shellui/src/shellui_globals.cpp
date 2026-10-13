/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ShellUI process globals — ToolboxUiState definition.
 */

#include "shellui_state.hpp"
#include "debug_settings_route_runtime.hpp"

#include <onion/debug_settings_route_policy.hpp>
#include <onion/settings.hpp>

namespace {

onion::debug_settings_route::DebugSettingsRoutePolicy g_debug_settings_route;

} // namespace

onion::Settings g_settings;
OverlayLayout g_overlay_layout;

ToolboxUiState g_ui;

void shellui_configure_debug_settings_route(uint32_t system_version) {
  g_debug_settings_route =
      onion::debug_settings_route::DebugSettingsRoutePolicy::for_system_version(
          system_version);
}

bool shellui_debug_settings_uses_old_route(void) {
  return g_debug_settings_route.uses_old_route();
}

const char *shellui_debug_settings_toolbox_uri(void) {
  return g_debug_settings_route.toolbox_uri(
      onion::debug_settings_route::UriKind::WithMode);
}

const char *shellui_debug_settings_toolbox_uri_simple(void) {
  return g_debug_settings_route.toolbox_uri(
      onion::debug_settings_route::UriKind::Simple);
}

std::string shellui_rewrite_debug_settings_route(const std::string &uri) {
  return g_debug_settings_route.rewrite(uri);
}
