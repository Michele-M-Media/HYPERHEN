#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <bypervisor-5xx-source-dir> <output-elf>" >&2
  exit 2
fi

source_dir=$1
output_elf=$2

if [[ ! -f "$source_dir/Makefile" || ! -f "$source_dir/bypervisor5XX.c" || ! -f "$source_dir/offsets.h" ]]; then
  echo "invalid Byepervisor 5.xx source directory: $source_dir" >&2
  exit 2
fi

: "${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK must point to the PS5 Payload SDK}"

mkdir -p "$(dirname "$output_elf")"
make -C "$source_dir" clean
make -C "$source_dir" EXTRA_CFLAGS="-DHV5XX_KROP_THEFLOW_0506_NPT0 -DHV5XX_THEFLOW_DO_RELOAD=0 -DHV5XX_THEFLOW_POST_VERIFY_ALL=1"

built_elf="$source_dir/bypervisor5XX.elf"
if [[ ! -s "$built_elf" ]]; then
  echo "Byepervisor 5.xx build did not produce an ELF" >&2
  exit 1
fi

cp "$built_elf" "$output_elf"
{
  echo "component=Bypervisor-5xx"
  echo "profiles=5.00,5.02,5.10,5.50"
  echo "source_sha256=$(sha256sum "$source_dir/bypervisor5XX.c" | awk '{print $1}')"
  echo "offsets_sha256=$(sha256sum "$source_dir/offsets.h" | awk '{print $1}')"
} >"${output_elf}.provenance"

make -C "$source_dir" clean
