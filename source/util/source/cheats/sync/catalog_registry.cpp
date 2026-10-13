#include "cheats/sync/cheat_catalog_registry.hpp"

#include <cstring>

namespace onion::cheats::sync {

const ICheatCatalog &hen_cheats_collection();

const ICheatCatalog &CheatCatalogRegistry::primary() {
  return hen_cheats_collection();
}

const ICheatCatalog *CheatCatalogRegistry::find(const char *id) {
  if (!id || !id[0]) {
    return &primary();
  }
  const ICheatCatalog &primary_cat = primary();
  if (std::strcmp(id, primary_cat.id()) == 0) {
    return &primary_cat;
  }
  return nullptr;
}

} // namespace onion::cheats::sync
