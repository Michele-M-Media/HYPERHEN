#pragma once

/* Short, explicitly requested local capture and controller-speaker echo.
 * The caller serializes sessions and supplies a latched cancellation check.
 * Native audio calls are synchronous; deadlines are checked between calls. */
void hyper_p10_voice_echo(int user, bool (*session_active)(int),
                          bool (*notice)(const char *));
