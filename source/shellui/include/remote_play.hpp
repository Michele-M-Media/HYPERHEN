/* Copyright (C) 2025 OnionHEN / LightningMods
 * Remote Play dynamic page lifecycle and XML generation.
 */
#pragma once

#include "monodef.h"

#include <atomic>
#include <string>

namespace remote_play {

enum class PairingState : unsigned char {
  Idle,
  Waiting,
  Paired,
  Failed,
  Cancelled,
};

/** Claim the active pairing with exactly one terminal outcome. */
inline bool try_finish_pairing(std::atomic<PairingState> &state,
                               PairingState terminal_state) {
  PairingState expected = PairingState::Waiting;
  return state.compare_exchange_strong(expected, terminal_state,
                                       std::memory_order_acq_rel);
}

} // namespace remote_play

extern std::string g_remote_play_info;

void generate_remote_play_xml(std::string &xml_buffer);

/** Remember the managed SettingPage instance that owns Remote Play pairing. */
void remote_play_bind_page(MonoObject *page);

/** Stop page-owned work when the bound Remote Play page leaves the stack. */
bool remote_play_handle_popping(MonoObject *outgoing);
