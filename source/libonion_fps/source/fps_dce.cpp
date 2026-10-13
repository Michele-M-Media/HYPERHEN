/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_dce.hpp>

#include <onion/log.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace onion {
namespace fps {
namespace {

constexpr unsigned long kDceIoctlZero = 0x0000000080308217UL;
/* PHU 0x3F337 passes this exact sign-extended request value. */
constexpr unsigned long kDceIoctlPhu = 0xFFFFFFFF80308217UL;
constexpr uint64_t kArg0 = 0x10000000AULL;
constexpr uint64_t kArg1 = 0x8000000000ULL;
constexpr size_t kOutBytes = 0x60;
constexpr size_t kCountOff = 8; /* PHU: outbuf+8 is the flip counter */

struct DceIoctlArg {
  uint64_t selector;
  uint64_t mask;
  uint64_t output;
  uint64_t reserved[3];
};

static_assert(sizeof(DceIoctlArg) == 0x30,
              "DCE ioctl argument must match PHU's 0x30-byte stack region");

void set_status(DceSampleStatus *out, DceSampleStatus status) {
  if (out)
    *out = status;
}

} // namespace

const char *dce_sample_status_name(DceSampleStatus status) {
  switch (status) {
  case DceSampleStatus::NotAttempted:
    return "not-attempted";
  case DceSampleStatus::Ok:
    return "ok";
  case DceSampleStatus::InvalidArgument:
    return "invalid-argument";
  case DceSampleStatus::Unavailable:
    return "unavailable";
  case DceSampleStatus::OpenFailed:
    return "open-failed";
  case DceSampleStatus::IoctlFailed:
    return "ioctl-failed";
  }
  return "unknown";
}

DceSource::~DceSource() { close(); }

const char *DceSource::request_abi_name() const {
  switch (request_abi_) {
  case 1:
    return "zero-extended";
  case 2:
    return "phu-sign-extended";
  default:
    return "probing";
  }
}

void DceSource::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool DceSource::open() {
  if (unavailable_)
    return false;
  if (fd_ >= 0)
    return true;

  fd_ = ::open("/dev/dce", O_RDWR);
  if (fd_ < 0) {
    const int err = errno;
    last_errno_ = err;
    if (err == EPERM || err == EACCES || err == ENOENT)
      unavailable_ = true;
    if (!logged_fail_) {
      LOG_WARN("fps: open /dev/dce failed: %s", std::strerror(err));
      logged_fail_ = true;
    }
    return false;
  }
  last_errno_ = 0;
  LOG_INFO("fps: /dev/dce opened (scanout)");
  return true;
}

bool DceSource::sample(uint64_t *count, DceSampleStatus *status) {
  if (!count) {
    set_status(status, DceSampleStatus::InvalidArgument);
    return false;
  }
  if (unavailable_) {
    set_status(status, DceSampleStatus::Unavailable);
    return false;
  }
  if (fd_ < 0 && !open()) {
    set_status(status, unavailable_ ? DceSampleStatus::Unavailable
                                    : DceSampleStatus::OpenFailed);
    return false;
  }

  unsigned char out[kOutBytes] {};
  auto run_ioctl = [&](unsigned long request) -> int {
    std::memset(out, 0, sizeof(out));
    DceIoctlArg arg {};
    arg.selector = kArg0;
    arg.mask = kArg1;
    arg.output = reinterpret_cast<uint64_t>(out);
    return ioctl(fd_, request, &arg);
  };

  unsigned long request =
      request_abi_ == 2 ? kDceIoctlPhu : kDceIoctlZero;
  int rc = run_ioctl(request);
  int err = rc < 0 ? errno : 0;

  /* Some PS5 syscall paths reject the conventional zero extension. PHU uses
   * the sign-extended value, so retry it on EINVAL and remember what worked. */
  if (rc < 0 && err == EINVAL && request != kDceIoctlPhu) {
    request = kDceIoctlPhu;
    rc = run_ioctl(request);
    err = rc < 0 ? errno : 0;
  }

  if (rc < 0) {
    last_errno_ = err;
    if (err == EBADF) {
      close();
      set_status(status, DceSampleStatus::IoctlFailed);
      return false;
    }
    if (!logged_fail_) {
      LOG_WARN("fps: ioctl(0x80308217) failed: %s", std::strerror(err));
      logged_fail_ = true;
    }
    /* PHU keeps the fd and retries the same request on the next sample. */
    set_status(status, DceSampleStatus::IoctlFailed);
    return false;
  }

  request_abi_ = request == kDceIoctlPhu ? 2 : 1;
  if (!logged_abi_) {
    LOG_INFO("fps: /dev/dce ioctl active abi=%s arg_bytes=%u out_bytes=%u",
             request_abi_name(), static_cast<unsigned>(sizeof(DceIoctlArg)),
             static_cast<unsigned>(sizeof(out)));
    logged_abi_ = true;
  }
  std::memcpy(count, out + kCountOff, sizeof(*count));
  last_errno_ = 0;
  set_status(status, DceSampleStatus::Ok);
  return true;
}

} // namespace fps
} // namespace onion
