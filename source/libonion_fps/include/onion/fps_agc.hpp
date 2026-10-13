/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Tier 1E DCB ring + Tier 1F AgcDriver global submit counter.
 * Follows PHU Games Tools by ArkSama (https://github.com/ArkSama).
 */
#pragma once

#include <cstdint>
#include <sys/types.h>

#include <onion/proc_dmap.h>

namespace onion {
namespace fps {

enum class AgcSampleStatus : uint8_t {
  NotAttempted,
  Ok,
  InvalidArgument,
  DmapInitFailed,
  RingSizeTranslateFailed,
  RingIndexTranslateFailed,
  RingEntriesTranslateFailed,
  RingSizeReadFailed,
  RingIndexReadFailed,
  RingSlotTranslateFailed,
  RingSlotReadFailed,
  RingNoNonzeroSlot,
  DriverHandleFailed,
  DriverBaseFailed,
  GlobalTranslateFailed,
  GlobalReadFailed,
};

const char *agc_sample_status_name(AgcSampleStatus status);

class AgcSources {
public:
  void reset();

  bool sample_ring(pid_t pid, uint64_t *count,
                   AgcSampleStatus *status = nullptr);
  bool sample_global(pid_t pid, uint64_t *count,
                     AgcSampleStatus *status = nullptr);
  uint32_t ring_size() const { return ring_size_; }
  uint32_t ring_write_index() const { return ring_write_idx_; }
  uint64_t global_va() const { return global_va_; }

private:
  void invalidate_cached_map();

  pid_t pid_ = -1;
  uint64_t global_va_ = 0;
  uint64_t global_phys_ = 0;
  uint64_t ring_size_phys_ = 0;
  uint64_t write_idx_phys_ = 0;
  uint64_t entries_phys_ = 0;
  uint32_t ring_size_ = 0;
  uint32_t ring_write_idx_ = 0;
  bool ring_ready_ = false;
  onion_proc_dmap_ctx dmap_{};
  bool logged_ring_ = false;
  bool logged_global_ = false;
};

} // namespace fps
} // namespace onion
