/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

/** Ask crit daemon to inject Toolbox (util crash / re-launch). Rest resume
 *  is handled in daemon (SceSysCore NOTE_EXEC + sprx wait). */
bool toolbox_reinject();
