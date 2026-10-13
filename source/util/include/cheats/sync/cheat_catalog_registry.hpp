#pragma once

#include "cheats/sync/i_cheat_catalog.hpp"

namespace onion::cheats::sync {

/**
 * Static table of catalogs. Adding a source is one registration, not a
 * change to CheatSyncEngine.
 */
class CheatCatalogRegistry {
public:
  static const ICheatCatalog &primary();
  static const ICheatCatalog *find(const char *id);
};

} // namespace onion::cheats::sync
