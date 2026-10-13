/* Copyright (C) 2026 OnionHEN */
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace onion {

/**
 * Process-lifetime executable storage for detour trampolines.
 *
 * The arena deliberately uses non-fixed mmap hints.  MAP_FIXED|MAP_EXCL is
 * not reliable on every PS5 runtime, and a failed collision check can replace
 * an existing trampoline.  A bump allocator over a private mapping gives each
 * trampoline a unique slot and never unmaps an individual slot.
 */
class TrampolineArena final {
public:
  using ProtectFn = bool (*)(void *address, size_t length);

  explicit TrampolineArena(ProtectFn protect = nullptr) noexcept;
  ~TrampolineArena() noexcept;

  TrampolineArena(const TrampolineArena &) = delete;
  TrampolineArena &operator=(const TrampolineArena &) = delete;

  /** Allocate a page-aligned slot within signed-32-bit reach of target. */
  void *allocate_near(size_t length, uintptr_t target) noexcept;

  /** True when address belongs to one of this arena's live mappings. */
  bool owns(const void *address) const noexcept;

private:
  struct Mapping {
    uintptr_t base = 0;
    size_t length = 0;
    size_t used = 0;
  };

  struct Allocation {
    uintptr_t begin = 0;
    uintptr_t end = 0;
  };

  static constexpr size_t kMaxMappings = 8;
  static constexpr size_t kSlotsPerMapping = 128;
  static constexpr size_t kMaxAllocations = kMaxMappings * kSlotsPerMapping;
  static constexpr uintptr_t kMaxRel32Distance = 0x7fffffffULL;

  size_t page_size_;
  ProtectFn protect_;
  Mapping mappings_[kMaxMappings]{};
  size_t mapping_count_ = 0;
  Allocation allocations_[kMaxAllocations]{};
  size_t allocation_count_ = 0;
  mutable std::mutex mutex_;

  static size_t host_page_size() noexcept;
  static bool within_rel32(uintptr_t a, uintptr_t b) noexcept;
  static size_t align_up(size_t value, size_t alignment) noexcept;
  Mapping *map_near(uintptr_t target) noexcept;
  bool record_unique(uintptr_t address, size_t length) noexcept;
};

} // namespace onion
