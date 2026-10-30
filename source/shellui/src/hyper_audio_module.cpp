#include "hyper_audio_module.hpp"

#include <cerrno>
#include <cstring>
#include <ps5/kernel.h>

namespace {
constexpr const char kAudioIn[] = "libSceAudioIn.sprx";
constexpr const char kAudioOut[] = "libSceAudioOut.sprx";

bool permitted_module(const char *name) {
  return name && (std::strcmp(name, kAudioIn) == 0 ||
                  std::strcmp(name, kAudioOut) == 0);
}
} // namespace

int hyper_audio_module_handle(const char *name, uint32_t *handle) {
  if (!handle) return -EINVAL;
  *handle = 0;
  if (!permitted_module(name)) return -EINVAL;
  uint32_t resident = 0;
  const int result = kernel_dynlib_handle(-1, name, &resident);
  if (result != 0) return result < 0 ? result : -EIO;
  *handle = resident;
  return 0;
}

void *hyper_audio_module_symbol(uint32_t handle, const char *symbol) {
  if (!symbol) return nullptr;
  const char *module = nullptr;
  if (std::strncmp(symbol, "sceAudioOut", sizeof("sceAudioOut") - 1) == 0)
    module = kAudioOut;
  else if (std::strncmp(symbol, "sceAudioIn", sizeof("sceAudioIn") - 1) == 0)
    module = kAudioIn;
  else
    return nullptr;

  // Recheck identity instead of caching handles across process/module changes.
  uint32_t resident = 0;
  if (hyper_audio_module_handle(module, &resident) != 0 || resident != handle)
    return nullptr;
  return reinterpret_cast<void *>(kernel_dynlib_dlsym(-1, handle, symbol));
}
