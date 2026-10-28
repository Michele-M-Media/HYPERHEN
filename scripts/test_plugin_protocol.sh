#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_tmp="$(mktemp -d /tmp/hyperhen-plugin-protocol.XXXXXX)"
cc -std=c17 -D_DEFAULT_SOURCE -DONION_ELFLDR_PORT=39020 -Wall -Wextra -Werror \
  -I "${task_root}/source/include" \
  "${task_root}/tools/plugin_protocol_check.c" \
  "${task_root}/source/common/elfldr_remote.c" -pthread -o "${task_tmp}/protocol-check"
"${task_tmp}/protocol-check"
