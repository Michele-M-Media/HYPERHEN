#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_tmp="$(mktemp -d /tmp/hyperhen-plugin-transport.XXXXXX)"
cc -O1 -c "${task_root}/third_party/cheat_support/sha256.c" -o "${task_tmp}/sha.o"
c++ -std=c++20 -D_DEFAULT_SOURCE -DONION_HOST_TEST=1 -DONION_LOG_COMPILE_LEVEL=0 \
  -DHYPERHEN_PLUGIN_ATTEMPT_ROOT=\"${task_tmp}/receipts\" -g -O1 -Wall -Wextra \
  -I "${task_root}/tools/plugin-runtime-stubs" \
  -I "${task_root}/source/libonion_sprx/include" \
  -I "${task_root}/source/libonion_platform/include" \
  -I "${task_root}/source/include" \
  -I "${task_root}/third_party/tachen-plugin-runtime" \
  -I "${task_root}/third_party/cheat_support" \
  "${task_root}/tools/plugin_transport_check.cpp" \
  "${task_root}/source/libonion_sprx/source/ptrace_sprx_runtime.cpp" \
  "${task_tmp}/sha.o" -pthread -o "${task_tmp}/transport-check"
"${task_tmp}/transport-check"
