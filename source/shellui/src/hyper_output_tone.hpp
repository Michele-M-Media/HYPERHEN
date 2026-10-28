#pragma once

// Explicitly requested synthetic controller-speaker test. The caller serializes
// sessions and rejects cancellation, stale input or a changed foreground user.
void hyper_p12_output_tone(int user, bool (*active)(int),
                           bool (*notice)(const char *));
