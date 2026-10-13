/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ShellUI IPC client entry — no HookedFuncs (true compile seam).
 * Call sites that need UI types/hooks include hooked_funcs.hpp separately.
 */

#pragma once

#include <onion/ipc_client.hpp>

// shellui-only IPC-adjacent globals
extern bool cheats_shortcut_activate;
