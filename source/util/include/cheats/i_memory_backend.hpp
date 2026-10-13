#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sys/types.h>

namespace onion::cheats {

/** Strategy: remote process memory access for patch apply. */
class IMemoryBackend {
public:
  virtual ~IMemoryBackend() = default;

  virtual int read(pid_t pid, uint64_t addr, void *buf, size_t len) = 0;
  virtual int write(pid_t pid, uint64_t addr, const void *buf, size_t len) = 0;
  virtual int mapCodeCave(pid_t pid, uint64_t addr, size_t len) = 0;
};

class MemoryBackendFactory {
public:
  /** Pick backend from Prospero FW major (util_system_fw_major). */
  static std::unique_ptr<IMemoryBackend> create(uint32_t fw_major);
};

} // namespace onion::cheats
