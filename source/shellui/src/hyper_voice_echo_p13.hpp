#pragma once

// A single explicitly requested, local capture/replay diagnostic. No file,
// network, speech-recognition or global audio-setting operations.
void hyper_p13_voice_echo(int user, bool (*active)(int),
                         bool (*notice)(const char *));
