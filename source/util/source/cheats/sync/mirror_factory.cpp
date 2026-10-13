#include "cheats/sync/cheat_mirror_factory.hpp"

#include <onion/notify_i18n.h>

#include <cctype>
#include <cstring>

namespace onion::cheats::sync {
namespace {

bool iequals(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

} // namespace

std::unique_ptr<ICheatMirror> CheatMirrorFactory::make(CheatMirrorId id) {
  if (id == CheatMirrorId::Cnb) {
    return make_cnb_mirror();
  }
  return make_github_mirror();
}

CheatMirrorPref CheatMirrorFactory::parsePref(const char *token,
                                              CheatMirrorPref def) {
  if (iequals(token, "auto")) {
    return CheatMirrorPref::Auto;
  }
  if (iequals(token, "github")) {
    return CheatMirrorPref::Github;
  }
  if (iequals(token, "cnb") || iequals(token, "cnb.cool")) {
    return CheatMirrorPref::Cnb;
  }
  return def;
}

const char *CheatMirrorFactory::prefName(CheatMirrorPref pref) {
  switch (pref) {
  case CheatMirrorPref::Github:
    return "github";
  case CheatMirrorPref::Cnb:
    return "cnb";
  case CheatMirrorPref::Auto:
  default:
    return "auto";
  }
}

CheatMirrorPick CheatMirrorFactory::create(CheatMirrorPref pref, int ui_lang,
                                           int system_lang) {
  CheatMirrorPick pick;
  if (pref == CheatMirrorPref::Github) {
    pick.primary = make(CheatMirrorId::Github);
    return pick;
  }
  if (pref == CheatMirrorPref::Cnb) {
    pick.primary = make(CheatMirrorId::Cnb);
    return pick;
  }

  const onion_notify_language_t lang =
      onion_notify_resolve_language(ui_lang, system_lang);
  if (lang == ONION_NOTIFY_LANG_ZH_HANS) {
    pick.primary = make(CheatMirrorId::Cnb);
    pick.fallback = make(CheatMirrorId::Github);
  } else {
    pick.primary = make(CheatMirrorId::Github);
    pick.fallback = make(CheatMirrorId::Cnb);
  }
  return pick;
}

} // namespace onion::cheats::sync
