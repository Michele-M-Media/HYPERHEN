#pragma once

#include <cstdint>

// Borrow only the current process's already-loaded AudioIn/AudioOut modules.
// No load, reference acquisition, unload, or global audio teardown is performed.
int hyper_audio_module_handle(const char *name, uint32_t *handle);
void *hyper_audio_module_symbol(uint32_t handle, const char *symbol);
