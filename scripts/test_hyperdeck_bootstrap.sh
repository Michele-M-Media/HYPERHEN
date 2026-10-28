#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
stage_build="$(mktemp -d /tmp/hyperdeck-bootstrap.XXXXXX)"
server_elf="${1:-$project_dir/build-hyperdeck/bin/hyperdeck-websrv.elf}"
for media_launcher in 0 1; do
cc -std=gnu11 -Wall -Wextra -Werror -DONION_HOST_TEST=1 -DHYPERHEN_HYPERDECK_MEDIA_LAUNCHER="$media_launcher" -I"$project_dir/source/bootstrapper/include" \
  -I"$project_dir/source/include" -I"$project_dir/source/libonion_platform/include" \
  -I"$project_dir/source/libonion_proc/include" -DHYPERHEN_WEBDECK_ELF=\""$server_elf"\" \
  "$project_dir/source/bootstrapper/source/bootstrap_hyperdeck.c" \
  "$project_dir/tools/bootstrap_hyperdeck_check.c" -Wl,--wrap=access -Wl,--wrap=usleep -Wl,--wrap=clock_gettime -Wl,--wrap=kill \
  -o "$stage_build/bootstrap-check"
"$stage_build/bootstrap-check"
done
cc -std=gnu11 -Wall -Wextra -Werror -I"$project_dir/source/include" \
  "$project_dir/tools/boot_mode_5xx_check.c" -o "$stage_build/boot-mode-5xx-check"
"$stage_build/boot-mode-5xx-check"
