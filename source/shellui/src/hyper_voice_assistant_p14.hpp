#pragma once
#include <cstdint>
void hyper_p14_voice_assistant(int user, uint64_t sequence,
    bool (*active)(int), bool (*notice)(const char *),
    bool (*processing_active)(int), bool (*fresh_input)(int),
    bool (*notice_allowed)(int));
