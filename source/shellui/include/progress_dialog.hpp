/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Cheat-download progress page backed by a Legacy Settings user_custom Panel.
 * The worker thread only polls IPC. UI3 widget creation and updates stay on the
 * ShellUI thread.
 */
#pragma once

#include "monodef.h"

#include <cstdint>
#include <string>
#include <string_view>

/** Reset progress state and start the detached IPC status poller. */
void cheat_progress_show(uint32_t task_id);

/** Push cheat_progress.xml on the active Legacy Settings page stack. */
bool cheat_progress_open_page(void);

/** Generate the dynamic cheat_progress.xml document. */
void generate_cheat_progress_xml(std::string &xml_buffer);

/** Append the UI3 Panel to the Widget created for the user_custom element. */
void cheat_progress_attach_panel(std::string_view id, MonoObject *widget);

/** Remember the managed SettingPage instance that owns the progress UI. */
void cheat_progress_bind_page(MonoObject *page);

/**
 * Invalidate and clear the progress session when its owning page is popped.
 * Returns true only when @p outgoing is the bound progress page.
 */
bool cheat_progress_handle_popping(MonoObject *outgoing);

/** Apply pending status and percentage to real UI3 widgets on the UI thread. */
void shellui_poll_cheat_progress(void);
