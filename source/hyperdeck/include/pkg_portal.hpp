/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace hyperdeck::pkg {
inline constexpr char kHelperPath[]="/data/HyperHEN/bin/hyperhen-pkg-install.elf";
inline constexpr char kHelperName[]="hyperhen-pkg-install.elf";
struct Volume {std::string name,path,kind;uint64_t total=0,used=0,available=0;};
std::vector<Volume> volumes();
std::vector<std::string> roots();
bool helper_available();
bool prepare_helper();
int launch(const std::string &path);
void reap();
void scan();
std::string snapshot_json();
std::string selected_path(const std::string &args);
}
