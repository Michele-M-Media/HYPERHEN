#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <legacy-hen-source-dir> <output-elf>" >&2
  exit 2
fi

source_dir=$1
output_elf=$2

if [[ ! -f "$source_dir/Makefile" || ! -f "$source_dir/main.cpp" || ! -f "$source_dir/offsets.h" ]]; then
  echo "invalid legacy HEN source directory: $source_dir" >&2
  exit 2
fi

: "${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK must point to the PS5 Payload SDK}"

mkdir -p "$(dirname "$output_elf")"
make -C "$source_dir" clean
make -C "$source_dir"

built_elf="$source_dir/ps5-hen.elf"
if [[ ! -s "$built_elf" ]]; then
  echo "legacy HEN build did not produce an ELF" >&2
  exit 1
fi

cp "$built_elf" "$output_elf"
{
  echo "component=ps5-hen"
  echo "profiles=1.00,1.01,1.02,1.05,1.10,1.11,1.12,1.13,1.14,2.00,2.20,2.25,2.26,2.30,2.50,2.70,3.00,3.10,3.20,3.21,4.00,4.02,4.03,4.50,4.51"
  echo "source_sha256=$(sha256sum "$source_dir/main.cpp" | awk '{print $1}')"
  echo "offsets_sha256=$(sha256sum "$source_dir/offsets.h" | awk '{print $1}')"
} >"${output_elf}.provenance"

make -C "$source_dir" clean
