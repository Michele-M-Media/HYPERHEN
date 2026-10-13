/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Resolve toolbox control values for SettingPage.OnCreating (set_Value).
 */
#pragma once

#include <string>

/**
 * Resolve the display/value string for a control id.
 * Empty string means "not a known dynamic control".
 *
 * Used by OnPreCreate_Hook and dynamic page generators (payload/cheats).
 */
std::string resolve_toolbox_control_value(const std::string &id);
