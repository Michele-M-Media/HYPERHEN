/* Presentation-only localization for the engine selector. */
#pragma once
#include "hyperdeck_backend.hpp"
#include <onion/engine_selector.h>
#include <onion/notify_i18n.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#if defined(__SCE__) || defined(PS5)
extern "C" int sceSystemServiceParamGetInt(int, int *);
#endif
extern "C" const unsigned char *hyperdeck_selector_template(const char *, unsigned long *);

namespace selector_i18n {
inline void initialize() {
  int language = 1;
#if defined(__SCE__) || defined(PS5)
  if (sceSystemServiceParamGetInt(1, &language) < 0 || language < 0)
    language = 1;
#else
  const char *test_language = std::getenv("HYPERHEN_SYSTEM_LANGUAGE_TEST");
  if (test_language) {
    char *end = nullptr;
    const long parsed = std::strtol(test_language, &end, 10);
    if (end != test_language && !*end && parsed >= 0 && parsed <= 30)
      language = static_cast<int>(parsed);
  }
#endif
  onion_notify_apply_ui_language(0, language);
}

inline const char *direction() {
  return !std::strcmp(onion_notify_locale_id(), "ar") ? "rtl" : "ltr";
}

inline std::string html_escape(std::string_view input) {
  std::string output;
  for (char c : input) {
    switch (c) {
    case '&': output += "&amp;"; break;
    case '<': output += "&lt;"; break;
    case '>': output += "&gt;"; break;
    case '"': output += "&quot;"; break;
    case '\'': output += "&#39;"; break;
    default: output += c; break;
    }
  }
  return output;
}

inline std::string text(const char *key) {
  return html_escape(onion_notify_tr(key));
}

inline std::string script_quote(std::string_view input) {
  const std::string quoted = hyperdeck::json_quote(input);
  std::string output;
  for (size_t index = 0; index < quoted.size(); ++index) {
    const unsigned char c = static_cast<unsigned char>(quoted[index]);
    /* Keep JSON strings safe if a caller embeds the rendered script in HTML,
       and keep the older WebView parser safe from Unicode line separators. */
    if (c == '<') output += "\\u003c";
    else if (c == '>') output += "\\u003e";
    else if (c == '&') output += "\\u0026";
    else if (c == 0xe2 && index + 2 < quoted.size() &&
             static_cast<unsigned char>(quoted[index + 1]) == 0x80 &&
             (static_cast<unsigned char>(quoted[index + 2]) == 0xa8 ||
              static_cast<unsigned char>(quoted[index + 2]) == 0xa9)) {
      output += static_cast<unsigned char>(quoted[index + 2]) == 0xa8
        ? "\\u2028" : "\\u2029";
      index += 2;
    } else output += static_cast<char>(c);
  }
  return output;
}

struct Context {
  bool legacy;
  bool supported;
  bool core_available;
  std::string family;
};

inline Context context(uint32_t firmware) {
  const bool legacy = onion_engine_selector_is_legacy(firmware);
  const bool supported = onion_engine_selector_supported(firmware);
  const unsigned major = (firmware >> 24) & 0xff;
  return {legacy, supported,
          supported && onion_engine_selector_hypercore_available(firmware),
          legacy && major >= 1 && major <= 4
            ? std::to_string(major) + ".xx" : "5.xx"};
}

inline const char *value(const std::string &key, const Context &ctx) {
  if (key == "locale") return onion_notify_locale_id();
  if (key == "direction") return direction();
  if (key == "selector.family") return ctx.family.c_str();
  if (key == "selector.first_token") return ctx.legacy ? "ps5hen" : "bypervisor";
  if (key == "selector.first_name") return ctx.legacy ? "PS5 HEN" : "Bypervisor";
  if (key == "selector.supported") return ctx.supported ? "true" : "false";
  if (key == "selector.core_available") return ctx.core_available ? "true" : "false";
  if (key == "selector.page_title")
    return onion_notify_tr(ctx.legacy ? "notify.selector.legacy_page_title"
                                      : "notify.selector.page_title");
  if (key == "selector.launch_title")
    return onion_notify_tr(ctx.legacy ? "notify.selector.legacy_launch_title"
                                      : "notify.selector.launch_title");
  if (key == "selector.first_badge")
    return onion_notify_tr(ctx.legacy ? "notify.selector.legacy_hen_badge"
                                      : "notify.selector.linux_badge");
  if (key == "selector.first_path")
    return onion_notify_tr(ctx.legacy ? "notify.selector.legacy_hen_path"
                                      : "notify.selector.hv_path");
  if (key == "selector.first_description")
    return onion_notify_tr(ctx.legacy ? "notify.selector.legacy_hen_description"
                                      : "notify.selector.hv_description");
  if (key == "selector.first_route")
    return onion_notify_tr(ctx.legacy ? "notify.selector.legacy_hen_route"
                                      : "notify.selector.hv_route");
  if (key == "selector.core_description")
    return onion_notify_tr(ctx.core_available ? "notify.selector.core_description"
                                              : "notify.selector.core_unavailable");
  if (key.compare(0, 16, "notify.selector.") == 0)
    return onion_notify_tr(key.c_str());
  return nullptr;
}

inline std::string render(std::string_view source, bool script,
                          uint32_t firmware = 0x05100000) {
  const Context ctx = context(firmware);
  std::string result;
  size_t offset = 0;
  while (offset < source.size()) {
    const size_t begin = source.find("{{", offset);
    if (begin == std::string_view::npos) {
      result.append(source.substr(offset));
      break;
    }
    result.append(source.substr(offset, begin - offset));
    const size_t end = source.find("}}", begin + 2);
    if (end == std::string_view::npos) {
      result.append(source.substr(begin));
      break;
    }
    const std::string key(source.substr(begin + 2, end - begin - 2));
    /* Only these fixed attributes are markup; translated/context text is
       escaped below, including names, firmware labels and mode tokens. */
    if (!script && (key == "selector.first_attributes" ||
                    key == "selector.core_attributes")) {
      const bool enabled = key == "selector.first_attributes"
        ? ctx.supported : ctx.core_available;
      if (!enabled) result += " aria-disabled=\"true\"";
    } else if (const char *resolved = value(key, ctx))
      result += script ? script_quote(resolved) : html_escape(resolved);
    else
      result.append(source.substr(begin, end + 2 - begin));
    offset = end + 2;
  }
  return result;
}
}
