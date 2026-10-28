/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "shellui_types.hpp"

struct GamePadData;

/* Separate experimental entry; the existing Home top-nav opens the Toolbox. */
void hypershell_request_open(void);

/* Consume navigation input while the transparent dashboard is visible. */
bool hypershell_handle_gamepad(GamePadData *data);

/* UI-thread entry point; called from HyperHEN's single Application.Update hook. */
void hypershell_frame(MonoObject *application);

