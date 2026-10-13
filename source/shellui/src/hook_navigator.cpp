/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include "hooked_funcs.hpp"
#include "debug_settings_route_runtime.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include <chrono>
#include <string>

/*
 * 11.6+ navigation notes (NPXS40008 + Legacy UI3):
 *
 *   function=debug_settings     → RN DebugSettingsScreen  (no toolbox XML)
 *   function=debug_settings_old → DebugSettingsOldScreen → Legacy SettingPage
 *                                 (GetManifestResourceStream → toolbox XML)
 *
 * DO NOT use GoToURI / ItemzLaunchByUri for in-Settings redirects — it relaunches
 * the RN settings app and drops ps5:settings:main (Back cannot return home).
 *
 * Use BootHelper.Boot for in-session navigation:
 *   main → debug settings old → SettingPage
 *
 * Debounce only (short window). A sticky "pending" flag is wrong: UpdateNavigationState
 * often never reports "DebugSettingsOldScreen" in ToString after a successful open, so
 * the flag stayed true forever → 2nd GetModel no-ops + skips orig → blank RN page.
 */


/** Ignore duplicate GetModel + UpdateNavigationState within this window. */
static constexpr auto kLegacyNavDebounce = std::chrono::milliseconds(750);
static std::chrono::steady_clock::time_point g_last_legacy_nav{};

static std::string MonoObjectToString(MonoObject *obj) {
  if (!obj || !mono_object_get_class)
    return "";

  MonoClass *klass = mono_object_get_class(obj);
  if (!klass)
    return "";

  MonoString *text = Invoke<MonoString *>(nullptr, klass, obj, "ToString");
  if (!text)
    return "";

  return Mono_to_String(text);
}

static bool within_legacy_nav_debounce() {
  const auto now = std::chrono::steady_clock::now();
  if (g_last_legacy_nav.time_since_epoch().count() != 0 &&
      (now - g_last_legacy_nav) < kLegacyNavDebounce) {
    return true;
  }
  g_last_legacy_nav = now;
  return false;
}

static void clear_legacy_nav_debounce(const char *why) {
  g_last_legacy_nav = {};
  LOG_DEBUG("[DBG-NAV] debounce cleared (%s)", why);
}

/** In-app navigate to legacy DebugSettingsOldScreen (not a full ShellUI re-launch). */
static void navigate_legacy_debug_settings(const char *reason) {
  if (within_legacy_nav_debounce()) {
    LOG_WARN("[DBG-NAV] debounce skip (%s)", reason);
    return;
  }

  MonoDomain *dom = (mono_domain_get ? mono_domain_get() : nullptr);
  if (!dom)
    dom = Root_Domain;
  if (!dom || !mono_string_new) {
    LOG_WARN("[DBG-NAV] no domain; fallback GoToURI (%s)", reason);
    GoToURI(shellui_debug_settings_toolbox_uri_simple());
    return;
  }

  MonoString *uri =
      mono_string_new(dom, shellui_debug_settings_toolbox_uri_simple());
  if (!uri) {
    LOG_ERROR("[DBG-NAV] mono_string_new failed; fallback GoToURI (%s)", reason);
    GoToURI(shellui_debug_settings_toolbox_uri_simple());
    return;
  }

  if (boot_orig) {
    const bool ok = boot_orig(uri, 0, nullptr);
    LOG_DEBUG("[DBG-NAV] BootHelper.Boot(3) → legacy (%s) ret=%d", reason, ok ? 1 : 0);
    // If BootHelper failed, allow an immediate retry on next GetModel/nav tick.
    if (!ok)
      clear_legacy_nav_debounce("Boot(3) failed");
    return;
  }
  if (boot_orig_2) {
    const bool ok = boot_orig_2(uri, 0);
    LOG_DEBUG("[DBG-NAV] BootHelper.Boot(2) → legacy (%s) ret=%d", reason, ok ? 1 : 0);
    if (!ok)
      clear_legacy_nav_debounce("Boot(2) failed");
    return;
  }

  LOG_ERROR("[DBG-NAV] BootHelper missing; fallback GoToURI (%s)", reason);
  GoToURI(shellui_debug_settings_toolbox_uri_simple());
}

void ReactNavigatorManager_UpdateNavigationState_Hook(MonoObject *instance,
                                                      MonoObject *state) {
  if (!shellui_hooks_are_ready()) {
    if (ReactNavigatorManager_UpdateNavigationState_Orig)
      ReactNavigatorManager_UpdateNavigationState_Orig(instance, state);
    return;
  }

  std::string state_text = MonoObjectToString(state);

  if (!shellui_debug_settings_uses_old_route()) {
    if (ReactNavigatorManager_UpdateNavigationState_Orig)
      ReactNavigatorManager_UpdateNavigationState_Orig(instance, state);
    return;
  }

  // Back on settings home → next toolbox open must not be debounced as "duplicate".
  if (state_text.find("ps5:settings:main") != std::string::npos ||
      state_text.find("CategoriesScreen") != std::string::npos) {
    if (g_last_legacy_nav.time_since_epoch().count() != 0)
      clear_legacy_nav_debounce("settings main/categories");
  }

  if (state_text.find("DebugSettingsOldScreen") != std::string::npos ||
      state_text.find("ps5:settings:debug settings old") != std::string::npos) {
    // Arrived on legacy host; keep short debounce so dual hooks don't double-Boot.
    // (timestamp already set by navigate; do not clear here.)
  }

  // Categories type:screen → DebugSettingsScreen never hits BootHelper URI rewrite.
  if (state_text.find("DebugSettingsScreen") != std::string::npos &&
      state_text.find("DebugSettingsOldScreen") == std::string::npos &&
      state_text.find("ps5:settings:debug settings old") == std::string::npos) {
    LOG_DEBUG("[DBG-NAV] block DebugSettingsScreen state apply");
    navigate_legacy_debug_settings("UpdateNavigationState");
    // Do not apply RN DebugSettings native scene.
    return;
  }

  if (ReactNavigatorManager_UpdateNavigationState_Orig)
    ReactNavigatorManager_UpdateNavigationState_Orig(instance, state);
}

void DebugSettings_GetModel_Hook(MonoObject *instance, MonoObject *param,
                                 MonoObject *promise) {
  if (!shellui_hooks_are_ready()) {
    if (DebugSettings_GetModel_Orig)
      DebugSettings_GetModel_Orig(instance, param, promise);
    return;
  }

  if (!shellui_debug_settings_uses_old_route()) {
    if (DebugSettings_GetModel_Orig)
      DebugSettings_GetModel_Orig(instance, param, promise);
    return;
  }

  std::string param_text;
  std::string page_id;

  if (param && mono_object_get_class) {
    MonoClass *param_class = mono_object_get_class(param);
    if (param_class) {
      MonoDomain *dom = (mono_domain_get ? mono_domain_get() : nullptr);
      if (!dom)
        dom = Root_Domain;
      MonoString *page_key =
          (dom && mono_string_new) ? mono_string_new(dom, "pageId") : nullptr;

      if (page_key) {
        MonoObject *page_token = Invoke<MonoObject *>(
            nullptr, param_class, param, "GetValue", page_key);
        if (page_token) {
          MonoClass *token_class = mono_object_get_class(page_token);
          if (token_class) {
            MonoString *page_string =
                Invoke<MonoString *>(nullptr, token_class, page_token, "ToString");
            if (page_string)
              page_id = Mono_to_String(page_string);
          }
        }
      }

      MonoString *param_string =
          Invoke<MonoString *>(nullptr, param_class, param, "ToString");
      if (param_string)
        param_text = Mono_to_String(param_string);
    }
  }

  if (!page_id.empty())
    LOG_DEBUG("[DBG-GETMODEL] pageId=%s", page_id.c_str());
  else
    LOG_DEBUG("[DBG-GETMODEL] pageId=<empty>");

  if (!param_text.empty())
    LOG_DEBUG("[DBG-GETMODEL] param=%s", param_text.c_str());
  else
    LOG_DEBUG("[DBG-GETMODEL] param=<empty>");

  // RN Debug Settings root — open legacy host; skip RN model (blank without data is OK
  // only if BootHelper actually runs). Never leave a sticky "pending" that blocks later
  // entries after the user has returned to main.
  if (page_id == "id_debug_settings" ||
      param_text.find("id_debug_settings") != std::string::npos) {
    LOG_DEBUG("[DBG-GETMODEL] id_debug_settings → BootHelper legacy");
    navigate_legacy_debug_settings("GetModel");
    return;
  }

  if (DebugSettings_GetModel_Orig)
    DebugSettings_GetModel_Orig(instance, param, promise);
}
