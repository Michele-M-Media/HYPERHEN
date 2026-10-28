/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <span>
#include <cstdint>
#include <string>
#include <string_view>
namespace onion::sprx {
bool parse_game_metadata(std::span<const uint8_t> bytes, std::string_view title,
                         std::string &version) noexcept;
}
