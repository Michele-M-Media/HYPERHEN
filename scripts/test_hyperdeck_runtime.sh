#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_build="$(mktemp -d /tmp/hyperdeck-runtime-check.XXXXXX)"
cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$project_dir/source/bootstrapper/include" -I"$project_dir/source/include" \
  -I"$project_dir/source/libonion_elfldr/include" -I"$project_dir/source/libonion_platform/include" \
  -I"$project_dir/tools/hyperdeck-launcher-stubs" \
  "$project_dir/source/bootstrapper/source/bootstrap_hyperdeck_runtime.c" \
  "$project_dir/tools/hyperdeck_runtime_check.c" \
  -Wl,--wrap=open -Wl,--wrap=sigaction -o "$test_build/runtime-check"
"$test_build/runtime-check"
node "$project_dir/tools/check_hyperdeck_probe.cjs" "$test_build/runtime-check"
