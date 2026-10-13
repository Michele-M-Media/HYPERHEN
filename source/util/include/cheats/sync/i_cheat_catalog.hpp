#pragma once

#include "cheats/sync/types.hpp"

#include <cstddef>

namespace onion::cheats::sync {

/**
 * Strategy: one cheat *source* (which repo), not which host.
 *
 * Implementations may mention owner/repo slugs and layout. Callers outside
 * the catalog .cpp must not hard-code those details.
 */
class ICheatCatalog {
public:
  virtual ~ICheatCatalog() = default;

  /** Stable filesystem-safe id, e.g. "hen-cheats-collection". */
  virtual const char *id() const = 0;

  virtual const char *defaultBranch() const = 0;

  /**
   * owner/repo slug used by ICheatMirror to build an archive URL.
   * Override per mirror when a host uses a different path.
   */
  virtual const char *slugFor(CheatMirrorId mirror) const = 0;

  /** Relative archive directories handed to the existing flatten installer. */
  virtual const char *const *flattenRoots(size_t *count) const = 0;
};

} // namespace onion::cheats::sync
