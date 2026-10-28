#pragma once

/* Observes only the native mute status, never PCM audio. The session callback
 * checks the local ShellUI input feed, cancellation and foreground user. */
void hyper_p9_monitor_mute(int user, bool (*session_active)(int),
                           bool (*notice)(const char *));
