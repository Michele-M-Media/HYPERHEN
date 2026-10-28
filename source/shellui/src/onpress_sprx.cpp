#include "onpress.hpp"

#include "external_sprx_ui.hpp"
#include "plugin_progress.hpp"
#include "plugin_sprx_pages.hpp"

namespace {

OnPressResult external_sprx_control(OnPressContext &ctx) {
  ctx.dirty = false;
  std::string id;
  if (onion::shellui::plugin_pages::split_control_id(
          ctx.id, onion::shellui::plugin_pages::kSprxEnabledPrefix, id, 1, 31)) {
    const bool start = ctx.value == "1" || ctx.value == "true";
    plugin_progress_show(id, start, true);
    if (!plugin_progress_open_page())
      notify("notify.sprx.operation_failed_fmt", id.c_str());
    return OnPressResult::Consumed;
  }
  const onion::shellui::external_sprx::DispatchResult result =
      onion::shellui::external_sprx::dispatch(ctx.id, ctx.value);
  if (!result.owned) return OnPressResult::NotMine;

  if (result.action == onion::shellui::external_sprx::Action::EnabledChanged &&
      result.success)
    return OnPressResult::Consumed;

  const char *key = result.success ? "notify.sprx.deleted_fmt"
                                   : "notify.sprx.operation_failed_fmt";
  notify(key, result.id.c_str());
  return OnPressResult::Consumed;
}

const OnPressPrefixEntry kSprxPrefix[] = {
    {"id_external_sprx_", external_sprx_control},
};

} // namespace

const OnPressPrefixEntry *onpress_sprx_prefix(size_t *count) {
  *count = sizeof(kSprxPrefix) / sizeof(kSprxPrefix[0]);
  return kSprxPrefix;
}
