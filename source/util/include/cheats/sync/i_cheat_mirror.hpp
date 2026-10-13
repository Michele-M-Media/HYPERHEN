#pragma once

#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/types.hpp"

#include <memory>
#include <string>

namespace onion::cheats::sync {

/** One HTTPS archive source. Repository identity remains in ICheatCatalog. */
class ICheatMirror {
public:
  virtual ~ICheatMirror() = default;

  virtual CheatMirrorId id() const = 0;
  virtual const char *name() const = 0;
  virtual const char *archiveHost() const = 0;
  virtual std::string archiveUrl(const ICheatCatalog &catalog) const = 0;
};

std::unique_ptr<ICheatMirror> make_github_mirror();
std::unique_ptr<ICheatMirror> make_cnb_mirror();

} // namespace onion::cheats::sync
