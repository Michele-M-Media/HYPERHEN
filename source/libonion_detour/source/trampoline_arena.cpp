/* Copyright (C) 2026 OnionHEN */

#include <onion/trampoline_arena.hpp>

#include <sys/mman.h>
#include <unistd.h>

#include <limits>

namespace onion {
namespace {

constexpr uintptr_t kHintStep = 64ULL * 1024ULL * 1024ULL;
constexpr unsigned kHintSteps = 31;

bool within_rel32(uintptr_t a, uintptr_t b) noexcept {
  constexpr uintptr_t kMaxRel32Distance = 0x7fffffffULL;
  return a >= b ? a - b <= kMaxRel32Distance : b - a <= kMaxRel32Distance;
}

bool valid_mapping(uintptr_t base, size_t length, uintptr_t target) noexcept {
  if (!base || length == 0 || base > std::numeric_limits<uintptr_t>::max() - length)
    return false;
  return within_rel32(base, target) && within_rel32(base + length - 1, target);
}

} // namespace

size_t TrampolineArena::host_page_size() noexcept {
  const long value = sysconf(_SC_PAGESIZE);
  return value > 0 ? static_cast<size_t>(value) : 4096u;
}

bool TrampolineArena::within_rel32(uintptr_t a, uintptr_t b) noexcept {
  return a >= b ? a - b <= kMaxRel32Distance : b - a <= kMaxRel32Distance;
}

size_t TrampolineArena::align_up(size_t value, size_t alignment) noexcept {
  if (!alignment || value > std::numeric_limits<size_t>::max() - (alignment - 1))
    return 0;
  return (value + alignment - 1) / alignment * alignment;
}

TrampolineArena::TrampolineArena(ProtectFn protect) noexcept
    : page_size_(host_page_size()), protect_(protect) {}

TrampolineArena::~TrampolineArena() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < mapping_count_; ++i) {
    if (mappings_[i].base && mappings_[i].length) {
      (void)munmap(reinterpret_cast<void *>(mappings_[i].base),
                   mappings_[i].length);
    }
  }
}

TrampolineArena::Mapping *TrampolineArena::map_near(uintptr_t target) noexcept {
  if (mapping_count_ >= kMaxMappings || page_size_ == 0)
    return nullptr;

  const size_t mapping_length =
      page_size_ * kSlotsPerMapping;
  for (unsigned step = 0; step <= kHintSteps; ++step) {
    const uintptr_t distance = static_cast<uintptr_t>(step) * kHintStep;
    const uintptr_t hints[2] = {
        step == 0 ? target : (target <= std::numeric_limits<uintptr_t>::max() - distance
                                  ? target + distance
                                  : 0),
        step == 0 ? 0 : (target >= distance ? target - distance : 0),
    };

    for (const uintptr_t hint : hints) {
      if (step != 0 && hint == 0)
        continue;
      const int map_prot = protect_ ? (PROT_READ | PROT_WRITE | PROT_EXEC)
                                    : (PROT_READ | PROT_WRITE);
      void *mapped = mmap(reinterpret_cast<void *>(hint), mapping_length,
                          map_prot,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (mapped == MAP_FAILED)
        continue;

      const uintptr_t base = reinterpret_cast<uintptr_t>(mapped);
      if (!valid_mapping(base, mapping_length, target) ||
          (protect_ && !protect_(mapped, mapping_length))) {
        (void)munmap(mapped, mapping_length);
        continue;
      }

      Mapping &slot = mappings_[mapping_count_++];
      slot.base = base;
      slot.length = mapping_length;
      slot.used = 0;
      return &slot;
    }
  }
  return nullptr;
}

bool TrampolineArena::record_unique(uintptr_t address, size_t length) noexcept {
  if (!address || length == 0 ||
      address > std::numeric_limits<uintptr_t>::max() - length ||
      allocation_count_ >= kMaxAllocations)
    return false;

  const uintptr_t end = address + length;
  for (size_t i = 0; i < allocation_count_; ++i) {
    const Allocation &other = allocations_[i];
    if (address < other.end && end > other.begin)
      return false;
  }
  allocations_[allocation_count_++] = {address, end};
  return true;
}

void *TrampolineArena::allocate_near(size_t length, uintptr_t target) noexcept {
  if (length == 0 || !target)
    return nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  const size_t slot_length = align_up(length, page_size_);
  if (slot_length == 0)
    return nullptr;

  for (size_t i = 0; i < mapping_count_; ++i) {
    Mapping &mapping = mappings_[i];
    if (!within_rel32(mapping.base, target) ||
        mapping.used > mapping.length ||
        slot_length > mapping.length - mapping.used)
      continue;

    const uintptr_t address = mapping.base + mapping.used;
    if (!within_rel32(address, target) ||
        !within_rel32(address + slot_length - 1, target))
      continue;
    if (!record_unique(address, slot_length))
      return nullptr;
    mapping.used += slot_length;
    return reinterpret_cast<void *>(address);
  }

  Mapping *mapping = map_near(target);
  if (!mapping)
    return nullptr;
  const uintptr_t address = mapping->base;
  if (!record_unique(address, slot_length))
    return nullptr;
  mapping->used = slot_length;
  return reinterpret_cast<void *>(address);
}

bool TrampolineArena::owns(const void *address) const noexcept {
  if (!address)
    return false;
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < mapping_count_; ++i) {
    const Mapping &mapping = mappings_[i];
    if (value >= mapping.base && value < mapping.base + mapping.length)
      return true;
  }
  return false;
}

} // namespace onion
