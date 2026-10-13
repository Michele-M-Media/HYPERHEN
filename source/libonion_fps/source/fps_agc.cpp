/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_agc.hpp>

#include <onion/log.h>
#include <onion/proc_dmap.h>

#include <ps5/kernel.h>

#include <cstdint>

namespace onion {
namespace fps {
namespace {

/* PHU videoout_dcb_ring::init: mov rbx, 0xFE03B72EC */
constexpr uint64_t kRingVa = 0xFE03B72ECULL;
constexpr uint32_t kRingOffSize = 0;
constexpr uint32_t kRingOffIdx = 4;
constexpr uint32_t kRingOffEntries = 0xC;
constexpr uint32_t kSlotBytes = 32;
constexpr uint32_t kSlotCountOff = 8;
constexpr uint32_t kDefaultRing = 64;
constexpr uint32_t kMaxRing = 0x1000;
constexpr uint32_t kWalkAllLimit = 100;
constexpr uint64_t kGlobalOff = 0x1AAC0;

void set_status(AgcSampleStatus *out, AgcSampleStatus status) {
  if (out)
    *out = status;
}

} // namespace

const char *agc_sample_status_name(AgcSampleStatus status) {
  switch (status) {
  case AgcSampleStatus::NotAttempted:
    return "not-attempted";
  case AgcSampleStatus::Ok:
    return "ok";
  case AgcSampleStatus::InvalidArgument:
    return "invalid-argument";
  case AgcSampleStatus::DmapInitFailed:
    return "dmap-init-failed";
  case AgcSampleStatus::RingSizeTranslateFailed:
    return "ring-size-translate-failed";
  case AgcSampleStatus::RingIndexTranslateFailed:
    return "ring-index-translate-failed";
  case AgcSampleStatus::RingEntriesTranslateFailed:
    return "ring-entries-translate-failed";
  case AgcSampleStatus::RingSizeReadFailed:
    return "ring-size-read-failed";
  case AgcSampleStatus::RingIndexReadFailed:
    return "ring-index-read-failed";
  case AgcSampleStatus::RingSlotTranslateFailed:
    return "ring-slot-translate-failed";
  case AgcSampleStatus::RingSlotReadFailed:
    return "ring-slot-read-failed";
  case AgcSampleStatus::RingNoNonzeroSlot:
    return "ring-no-nonzero-slot";
  case AgcSampleStatus::DriverHandleFailed:
    return "driver-handle-failed";
  case AgcSampleStatus::DriverBaseFailed:
    return "driver-base-failed";
  case AgcSampleStatus::GlobalTranslateFailed:
    return "global-translate-failed";
  case AgcSampleStatus::GlobalReadFailed:
    return "global-read-failed";
  }
  return "unknown";
}

void AgcSources::invalidate_cached_map() {
  global_va_ = 0;
  global_phys_ = 0;
  ring_size_phys_ = 0;
  write_idx_phys_ = 0;
  entries_phys_ = 0;
  ring_size_ = 0;
  ring_write_idx_ = 0;
  ring_ready_ = false;
  dmap_ = {};
}

void AgcSources::reset() {
  pid_ = -1;
  invalidate_cached_map();
}

bool AgcSources::sample_ring(pid_t pid, uint64_t *count,
                             AgcSampleStatus *status) {
  if (!count || pid <= 0) {
    set_status(status, AgcSampleStatus::InvalidArgument);
    return false;
  }

  if (pid_ != pid) {
    reset();
    pid_ = pid;
  }
  if (dmap_.pid != pid && onion_proc_dmap_init(pid, &dmap_) != 0) {
    set_status(status, AgcSampleStatus::DmapInitFailed);
    return false;
  }

  if (!ring_ready_) {
    if (onion_proc_translate(&dmap_, kRingVa + kRingOffSize,
                             &ring_size_phys_, nullptr) != 0) {
      set_status(status, AgcSampleStatus::RingSizeTranslateFailed);
      return false;
    }
    if (onion_proc_translate(&dmap_, kRingVa + kRingOffIdx, &write_idx_phys_,
                             nullptr) != 0) {
      set_status(status, AgcSampleStatus::RingIndexTranslateFailed);
      return false;
    }
    if (onion_proc_translate(&dmap_, kRingVa + kRingOffEntries, &entries_phys_,
                             nullptr) != 0) {
      set_status(status, AgcSampleStatus::RingEntriesTranslateFailed);
      return false;
    }
    if (onion_proc_copyout_phys(&dmap_, ring_size_phys_, &ring_size_,
                                sizeof(ring_size_)) != 0) {
      invalidate_cached_map();
      set_status(status, AgcSampleStatus::RingSizeReadFailed);
      return false;
    }
    if (ring_size_ == 0 || ring_size_ > kMaxRing)
      ring_size_ = kDefaultRing;
    ring_ready_ = true;
  }

  const uint32_t ring_size = ring_size_;

  uint32_t write_idx = 0;
  if (onion_proc_copyout_phys(&dmap_, write_idx_phys_, &write_idx,
                              sizeof(write_idx)) != 0) {
    invalidate_cached_map();
    set_status(status, AgcSampleStatus::RingIndexReadFailed);
    return false;
  }
  ring_write_idx_ = write_idx;

  const uint64_t entries = kRingVa + kRingOffEntries;
  uint64_t best = 0;
  bool any = false;
  bool slot_translate_failed = false;
  bool slot_read_failed = false;

  auto read_slot = [&](uint32_t slot) -> bool {
    const uint64_t va =
        entries + static_cast<uint64_t>(slot) * kSlotBytes + kSlotCountOff;
    uint64_t v = 0;
    uint64_t phys = entries_phys_ + static_cast<uint64_t>(slot) * kSlotBytes +
                    kSlotCountOff;
    if (ring_size > kWalkAllLimit &&
        onion_proc_translate(&dmap_, va, &phys, nullptr) != 0) {
      slot_translate_failed = true;
      return false;
    }
    if (onion_proc_copyout_phys(&dmap_, phys, &v, sizeof(v)) != 0) {
      slot_read_failed = true;
      return false;
    }
    if (v == 0)
      return false;
    if (!any || v > best)
      best = v;
    any = true;
    return true;
  };

  if (ring_size > kWalkAllLimit) {
    for (int n = 1; n <= 4; ++n) {
      const uint32_t slot =
          static_cast<uint32_t>((static_cast<int>(ring_size) +
                                 static_cast<int>(write_idx) - n) %
                                static_cast<int>(ring_size));
      /* PHU uses the first non-zero slot preceding write_idx. */
      if (read_slot(slot))
        break;
    }
  } else {
    for (uint32_t i = 0; i < ring_size; ++i)
      (void)read_slot(i);
  }

  if (!any) {
    invalidate_cached_map();
    set_status(status, slot_translate_failed
                           ? AgcSampleStatus::RingSlotTranslateFailed
                           : (slot_read_failed
                                  ? AgcSampleStatus::RingSlotReadFailed
                                  : AgcSampleStatus::RingNoNonzeroSlot));
    return false;
  }
  if (!logged_ring_) {
    LOG_DEBUG("fps: DCB ring pid=%d size=%u", static_cast<int>(pid), ring_size);
    logged_ring_ = true;
  }
  *count = best;
  set_status(status, AgcSampleStatus::Ok);
  return true;
}

bool AgcSources::sample_global(pid_t pid, uint64_t *count,
                               AgcSampleStatus *status) {
  if (!count || pid <= 0) {
    set_status(status, AgcSampleStatus::InvalidArgument);
    return false;
  }
  if (pid_ != pid) {
    reset();
    pid_ = pid;
  }
  if (dmap_.pid != pid && onion_proc_dmap_init(pid, &dmap_) != 0) {
    set_status(status, AgcSampleStatus::DmapInitFailed);
    return false;
  }

  if (global_va_ == 0) {
    uint32_t handle = 0;
    if (kernel_dynlib_handle(pid, "libSceAgcDriver.sprx", &handle) != 0 ||
        handle == 0) {
      if (!logged_global_) {
        LOG_DEBUG("fps: libSceAgcDriver.sprx not in pid=%d",
                  static_cast<int>(pid));
        logged_global_ = true;
      }
      set_status(status, AgcSampleStatus::DriverHandleFailed);
      return false;
    }
    const intptr_t base = kernel_dynlib_mapbase_addr(pid, handle);
    if (base <= 0) {
      set_status(status, AgcSampleStatus::DriverBaseFailed);
      return false;
    }
    global_va_ = static_cast<uint64_t>(base) + kGlobalOff;
    if (onion_proc_translate(&dmap_, global_va_, &global_phys_, nullptr) != 0) {
      global_va_ = 0;
      global_phys_ = 0;
      set_status(status, AgcSampleStatus::GlobalTranslateFailed);
      return false;
    }
    LOG_DEBUG("fps: AgcDriver base=0x%lx counter=0x%lx",
              static_cast<unsigned long>(base),
              static_cast<unsigned long>(global_va_));
  }

  uint64_t v = 0;
  if (onion_proc_copyout_phys(&dmap_, global_phys_, &v, sizeof(v)) != 0) {
    invalidate_cached_map();
    set_status(status, AgcSampleStatus::GlobalReadFailed);
    return false;
  }
  *count = v;
  set_status(status, AgcSampleStatus::Ok);
  return true;
}

} // namespace fps
} // namespace onion
