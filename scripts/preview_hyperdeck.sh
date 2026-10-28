#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preview_build="$(mktemp -d /tmp/hyperdeck-preview.XXXXXX)"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -Wno-misleading-indentation \
  -I"${project_dir}/source/include" -I"${project_dir}/source/shellui/include" -I"${project_dir}/source/hyperdeck/include" \
  "${project_dir}/tools/hyperdeck_check.cpp" -o "${preview_build}/hyperdeck-check"
"${preview_build}/hyperdeck-check" --preview > "${project_dir}/assets/previews/hyperdeck-frames.js"
printf 'Open %s\n' "${project_dir}/assets/previews/hyperdeck.html"
