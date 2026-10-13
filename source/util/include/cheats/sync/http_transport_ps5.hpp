#pragma once

#include "cheats/sync/i_http_transport.hpp"

namespace onion::cheats::sync {

/** libcurl from the PS5 payload SDK (HTTP and HTTPS). */
class Ps5HttpTransport final : public IHttpTransport {
public:
  SyncStatus perform(
      const HttpRequest &req,
      const std::function<SyncStatus(const void *, size_t)> &on_data) override;
};

} // namespace onion::cheats::sync
