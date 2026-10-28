#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d /tmp/hyperhen-shellui-exec.XXXXXX)"
trap 'rm -f -- "$test_dir/policy-check"; rmdir -- "$test_dir"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic \
  -I"$project_dir/source/daemon/include" \
  "$project_dir/tools/shellui_exec_policy_check.cpp" \
  -o "$test_dir/policy-check"
"$test_dir/policy-check"
