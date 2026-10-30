#pragma once
#include "hyper_tts.hpp"
#include <string>

// Speak a coherent bounded reply. If a language needs more than the PCM budget,
// retry with fewer complete sentences/codepoints. The caller records only the
// text actually synthesized and acknowledged by the playback endpoint.
int hyper_reply_tts(std::string &reply, HyperTtsAudio &audio, const char *voice,
                    int style, bool (*cancel)(void *) = nullptr,
                    void *user = nullptr,
                    const char *data_path = "/data/Hyper/espeak-ng-data");
