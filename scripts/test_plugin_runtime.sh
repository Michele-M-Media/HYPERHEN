#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_build="${1:-${task_root}/build-release-compatible}"
task_tmp="$(mktemp -d /tmp/hyperhen-plugin-check.XXXXXX)"
cc -std=c17 -D_DEFAULT_SOURCE -g -O1 -Wall -Wextra \
  -I "${task_root}/source/libonion_sprx/include" \
  -I "${task_root}/third_party/tachen-plugin-runtime" \
  "${task_root}/tools/plugin_runtime_check.c" \
  "${task_root}/source/libonion_sprx/source/plugin_worker.c" \
  "${task_root}/third_party/tachen-plugin-runtime/elf-image.c" \
  "${task_root}/third_party/tachen-plugin-runtime/prx-image.c" \
  "${task_root}/third_party/tachen-plugin-runtime/game-info.c" \
  -o "${task_tmp}/plugin-check"
"${task_tmp}/plugin-check" "${task_build}/libonion_sprx/worker/worker.bin"
