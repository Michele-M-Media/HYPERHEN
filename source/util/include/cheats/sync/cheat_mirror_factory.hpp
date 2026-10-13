#pragma once

#include "cheats/sync/i_cheat_mirror.hpp"
#include "cheats/sync/types.hpp"

#include <memory>

namespace onion::cheats::sync {

struct CheatMirrorPick {
  std::unique_ptr<ICheatMirror> primary;
  std::unique_ptr<ICheatMirror> fallback;
};

class CheatMirrorFactory {
public:
  static CheatMirrorPick create(CheatMirrorPref pref, int ui_lang,
                                int system_lang);
  static std::unique_ptr<ICheatMirror> make(CheatMirrorId id);
  static CheatMirrorPref parsePref(const char *token, CheatMirrorPref def);
  static const char *prefName(CheatMirrorPref pref);
};

} // namespace onion::cheats::sync
