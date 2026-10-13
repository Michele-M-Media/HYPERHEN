#include "cheats/sync/i_cheat_mirror.hpp"

#include <memory>

namespace onion::cheats::sync {
namespace {

class GithubMirror final : public ICheatMirror {
public:
  CheatMirrorId id() const override { return CheatMirrorId::Github; }
  const char *name() const override { return "github"; }
  const char *archiveHost() const override { return "codeload.github.com"; }
  std::string archiveUrl(const ICheatCatalog &catalog) const override {
    std::string url = "https://codeload.github.com/";
    url += catalog.slugFor(id());
    url += "/zip/refs/heads/";
    url += catalog.defaultBranch();
    return url;
  }
};

} // namespace

std::unique_ptr<ICheatMirror> make_github_mirror() {
  return std::unique_ptr<ICheatMirror>(new GithubMirror());
}

} // namespace onion::cheats::sync
