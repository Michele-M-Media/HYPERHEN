#pragma once

#include "cheats/sync/types.hpp"

#include <cstddef>
#include <functional>

namespace onion::cheats::sync {

using HttpProgressFn = void (*)(size_t received, size_t total, void *user);

struct HttpRequest {
  const char *url = nullptr;
  const char *method = "GET";
  const char *content_type = nullptr;
  const char *accept = nullptr;
  const char *user_agent = nullptr;
  const char *host_allow = nullptr;
  const void *body = nullptr;
  size_t body_len = 0;
  int timeout_ms = 0;
  int status_min = 200;
  int status_max = 299;
  size_t max_body_bytes = 0;
  HttpProgressFn on_progress = nullptr;
  void *progress_user = nullptr;
  SyncCancelFn should_cancel = nullptr;
  void *cancel_user = nullptr;
};

/**
 * Strategy: HTTP(S) byte pipe for probes and git smart HTTP.
 * No git and no catalog types.
 */
class IHttpTransport {
public:
  virtual ~IHttpTransport() = default;

  virtual SyncStatus perform(
      const HttpRequest &req,
      const std::function<SyncStatus(const void *, size_t)> &on_data) = 0;
};

} // namespace onion::cheats::sync
