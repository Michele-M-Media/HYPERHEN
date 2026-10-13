#!/usr/bin/env bash
# Build the embedded HyperHEN hypervisor stage from vendored Byepervisor source.

set -euo pipefail

SOURCE_DIR="${1:-}"
OUTPUT_ELF="${2:-}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
HYPERHEN_JOBS="${HYPERHEN_JOBS:-4}"

die() {
  printf 'build_hypervisor: %s\n' "$*" >&2
  exit 1
}

[[ -n "${SOURCE_DIR}" && -d "${SOURCE_DIR}" ]] ||
  die "missing Byepervisor source directory: ${SOURCE_DIR:-<unset>}"
[[ -n "${OUTPUT_ELF}" ]] || die "missing output ELF path"
[[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] ||
  die "PS5_PAYLOAD_SDK must point to an installed PS5 Payload SDK"
[[ "${HYPERHEN_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
  die "HYPERHEN_JOBS must be a positive integer"

for required in Makefile main.c hyperhen_integration.c \
  break_hv_payload/break_hv.c break_hv_payload/entry.S; do
  [[ -f "${SOURCE_DIR}/${required}" ]] ||
    die "vendored dependency is incomplete: ${required}"
done

export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"

make -C "${SOURCE_DIR}" -j"${HYPERHEN_JOBS}"

BUILT_ELF="${SOURCE_DIR}/byepervisor_reborn.elf"
[[ -s "${BUILT_ELF}" ]] || die "build completed without ${BUILT_ELF}"

mkdir -p "$(dirname "${OUTPUT_ELF}")"
cp -f "${BUILT_ELF}" "${OUTPUT_ELF}"
printf 'HyperHEN hypervisor source: Byepervisor Reborn commit ea04f7132b837087f552c57261cb800481a3a910\n' \
  > "${OUTPUT_ELF}.provenance"

printf 'build_hypervisor: wrote %s (%s bytes)\n' \
  "${OUTPUT_ELF}" "$(wc -c < "${OUTPUT_ELF}" | tr -d ' ')"
