#include "hyperdpi_i18n.h"

#include <stddef.h>
#include <string.h>

#include "pkgserver_adapter.h"

extern int sceSystemServiceParamGetInt(int param_id, int *value);

#include "hyperdpi_i18n_catalog.inc"

static int g_locale_index;
static int g_initialized;

static const char *locale_id_for_system_language(int language) {
    switch (language) {
    case 0:
        return "ja";
    case 2:
    case 22:
        return "fr";
    case 3:
    case 20:
        return "es";
    case 4:
        return "de";
    case 5:
        return "it";
    case 7:
    case 17:
        return "pt-BR";
    case 8:
        return "ru";
    case 9:
        return "ko";
    case 10:
        return "zh-Hant";
    case 11:
        return "zh-Hans";
    case 16:
        return "pl";
    case 21:
        return "ar";
    case 27:
        return "th";
    default:
        return "en";
    }
}

static int locale_index(const char *id) {
    for (int index = 0; index < HYPERDPI_I18N_LOCALE_COUNT; ++index) {
        if (strcmp(kHyperDpiLocaleIds[index], id) == 0) return index;
    }
    return 0;
}

void hyperdpi_i18n_refresh(void) {
    int language = 1;
    if (sceSystemServiceParamGetInt(1, &language) < 0 && g_initialized) return;
    g_locale_index = locale_index(locale_id_for_system_language(language));
    g_initialized = 1;
    pkg_server_set_webui_lang(kHyperDpiLocaleIds[g_locale_index]);
}

void hyperdpi_i18n_initialize(void) {
    hyperdpi_i18n_refresh();
}

const char *hyperdpi_i18n_language_code(void) {
    return kHyperDpiLocaleIds[g_locale_index];
}

const char *hyperdpi_i18n_translate(const char *key) {
    if (!key) return "";
    for (size_t index = 0;
         index < sizeof(kHyperDpiTranslations) / sizeof(kHyperDpiTranslations[0]);
         ++index) {
        if (strcmp(kHyperDpiTranslations[index].key, key) == 0) {
            return kHyperDpiTranslations[index].text[g_locale_index];
        }
    }
    return key;
}
