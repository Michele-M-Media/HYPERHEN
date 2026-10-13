/* Copyright (C) 2025 OnionHEN / LightningMods

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
<http://www.gnu.org/licenses/>.  */

#pragma once

#include <onion/hotpatch.h>

#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

#define HOOK_LENGTH 14

/*
 * Two-phase detour handle. Prepare builds the trampoline and immutable patch
 * plan; Commit is the only operation that mutates the target function.
 */
struct DetourHandle {
  uint64_t target = 0;
  void *destination = nullptr;
  void *trampoline = nullptr;
  size_t trampoline_capacity = 0;
  size_t stolen_size = 0;
  size_t emitted_size = 0;
  onion_x64_atomic_patch patch{};
  bool prepared = false;
  bool committed = false;
};

bool PrepareDetour(uint64_t address, void *destination, DetourHandle *handle);
bool CommitDetour(DetourHandle *handle);
void AbortDetour(DetourHandle *handle);
bool InstallDetour(uint64_t address, void *destination,
                   void **original_storage);

/* Compatibility API for hooks that never call an original function. */
void PatchInJump(uint64_t address, void *destination);
void *DetourFunction(uint64_t address, void *destination);
void WriteMemory(uint64_t address, void *buffer, int length);
