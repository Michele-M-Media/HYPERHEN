#pragma once

// Reads the routes of two owned controller-speaker ports. No audio is supplied.
// The caller serializes sessions and rejects cancellation or a changed user.
void hyper_p11_probe_output_route(int user, bool (*session_active)(int),
                                  bool (*notice)(const char *));
