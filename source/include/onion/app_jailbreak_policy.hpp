/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Title-ID policy for the sandbox-file app-jailbreak protocol.
 */
#pragma once

#include <onion/settings.hpp>

#include <string_view>

namespace onion::app_jailbreak {

enum class AllowlistMatch {
  None,
  Exact,
  Prefix,
};

inline AllowlistMatch match(std::string_view tid,
                            const AppJailbreakAllowlist &allowlist) {
  const std::size_t exact_count =
      allowlist.exact_title_id_count < allowlist.exact_title_ids.size()
          ? allowlist.exact_title_id_count
          : allowlist.exact_title_ids.size();
  for (std::size_t i = 0; i < exact_count; ++i) {
    if (tid == allowlist.exact_title_ids[i]) {
      return AllowlistMatch::Exact;
    }
  }

  const std::size_t prefix_count =
      allowlist.title_id_prefix_count < allowlist.title_id_prefixes.size()
          ? allowlist.title_id_prefix_count
          : allowlist.title_id_prefixes.size();
  for (std::size_t i = 0; i < prefix_count; ++i) {
    const std::string_view prefix = allowlist.title_id_prefixes[i];
    if (!prefix.empty() && tid.size() >= prefix.size() &&
        tid.compare(0, prefix.size(), prefix) == 0) {
      return AllowlistMatch::Prefix;
    }
  }
  return AllowlistMatch::None;
}

inline bool is_whitelisted(std::string_view tid,
                           const AppJailbreakAllowlist &allowlist) {
  return match(tid, allowlist) != AllowlistMatch::None;
}

inline const char *whitelist_reason(std::string_view tid,
                                    const AppJailbreakAllowlist &allowlist) {
  switch (match(tid, allowlist)) {
  case AllowlistMatch::Exact:
    return "exact";
  case AllowlistMatch::Prefix:
    return "prefix";
  case AllowlistMatch::None:
  default:
    return "none";
  }
}

} // namespace onion::app_jailbreak
