/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/types.h>
namespace onion::sprx {
struct GameIdentity {
  pid_t pid = -1;
  int64_t seconds = 0, microseconds = 0;
  std::string title;
  intptr_t rootdir = 0, jaildir = 0;
};
bool capture_game(pid_t pid, GameIdentity &identity) noexcept;
bool same_game(const GameIdentity &identity) noexcept;
bool game_version(const GameIdentity &identity, std::string &version,
                  std::string &metadata_hash) noexcept;
bool stage_module(const GameIdentity &identity, std::string_view source,
                  uint32_t host_sdk, std::string &path,
                  std::string &fingerprint, std::string &error) noexcept;
}
