#pragma once

struct HyperLocale {
    int system_id;
    const char *tag;
    const char *whisper_code;
    const char *voice;
    const char *name;
};

const HyperLocale &hyper_locale_for_system(int system_id);
int hyper_current_system_language();

enum class HyperMessage {
    ready, preparing, listening, thinking, finished, interrupted,
    restart_required, assets_missing, low_memory, already_running
};
const char *hyper_message(HyperMessage message, int system_id = -1);

// Exact local commands only. Other text must use the language model.
const char *hyper_fast_reply(const char *text, int system_id = -1);

#if defined(HYPER_LOCALE_TESTING) && !defined(HYPER_P5_PS5) && !defined(__SCE__) && !defined(PS5)
void hyper_locale_set_test_language(int system_id);
#endif
