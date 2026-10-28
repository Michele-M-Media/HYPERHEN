/* Copyright (C) 2025 etaHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.

kernel_mprotect is the only path used to make the target page writable. */

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include "cr_hotkey_signal.h"

#define CR_HOOK_LENGTH 14

/* Distinct from NULL - address was already hooked by an earlier session's
 * payload still resident in the target, not a fresh failure. */
#define CR_DETOUR_ALREADY_HOOKED ((void *)(intptr_t)-1)

void  cr_patch_in_jump(uint64_t address, void *destination);

/* Builds the trampoline and jump bytes for `address` but does NOT write them; `site` comes back for the
 * daemon to arm under a process suspend. Returns the trampoline, NULL on failure or CR_DETOUR_ALREADY_HOOKED. */
void *cr_detour_prepare(uint64_t address, void *destination, cr_hook_site_t *site);

/* Arms a prepared site with one atomic 16-byte compare-and-swap.
 * Returns 0 and sets site->done, or -1 with the target left untouched. */
int cr_detour_commit(cr_hook_site_t *site);
