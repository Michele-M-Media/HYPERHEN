/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Tier 1A: /dev/dce scanout counter (V-sync capped).
 * Follows PHU Games Tools by ArkSama (https://github.com/ArkSama).
 */
#pragma once

#include <cstdint>

namespace onion {
namespace fps {

enum class DceSampleStatus : uint8_t {
  NotAttempted,
  Ok,
  InvalidArgument,
  Unavailable,
  OpenFailed,
  IoctlFailed,
};

const char *dce_sample_status_name(DceSampleStatus status);

class DceSource {
public:
  DceSource() = default;
  DceSource(const DceSource &) = delete;
  DceSource &operator=(const DceSource &) = delete;
  ~DceSource();

  /** Open /dev/dce. False if the node is missing or MAC-denied. */
  bool open();
  void close();
  bool is_open() const { return fd_ >= 0; }
  /** Session-level fail: do not retry open every tick. */
  bool unavailable() const { return unavailable_; }
  int last_errno() const { return last_errno_; }
  const char *request_abi_name() const;

  /** Read the current flip count. False on ioctl error. */
  bool sample(uint64_t *count, DceSampleStatus *status = nullptr);

private:
  int fd_ = -1;
  int last_errno_ = 0;
  uint8_t request_abi_ = 0;
  bool unavailable_ = false;
  bool logged_fail_ = false;
  bool logged_abi_ = false;
};

} // namespace fps
} // namespace onion
