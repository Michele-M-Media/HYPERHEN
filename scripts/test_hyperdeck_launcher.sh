#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_build="$(mktemp -d /tmp/hyperdeck-launcher-test.XXXXXX)"
c++ -std=c++20 -Wall -Wextra -Werror -Wno-misleading-indentation \
  -I"$project_dir/tools/hyperdeck-launcher-stubs" -I"$project_dir/source/hyperdeck/include" \
  -I"$project_dir/source/include" -I"$project_dir/source/libonion_platform/include" \
  "$project_dir/source/hyperdeck/launcher_ps5.cpp" "$project_dir/tools/hyperdeck_launcher_check.cpp" \
  -Wl,--wrap=lstat -Wl,--wrap=mkdir -Wl,--wrap=access -Wl,--wrap=unlink \
  -Wl,--wrap=usleep -Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=dlclose -o "$test_build/launcher-check"
"$test_build/launcher-check"
