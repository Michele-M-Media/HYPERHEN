#!/usr/bin/env bash
# Build the embedded HyperHEN hypervisor stage from vendored Byepervisor source.

set -euo pipefail

SOURCE_DIR="${1:-}"
OUTPUT_ELF="${2:-}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
HYPERHEN_JOBS="${HYPERHEN_JOBS:-4}"
HYPERHEN_1270_OFFSETS_SHA256="8768441ab5023a391820cae2b56dcf14a0470f05ceb6404ed43015c15f42e007"

die() {
  printf 'build_hypervisor: %s\n' "$*" >&2
  exit 1
}

sha256_file() {
  local file="$1"
  local output
  local digest

  if command -v sha256sum >/dev/null 2>&1; then
    output="$(sha256sum "${file}" 2>/dev/null)" ||
      die "sha256sum failed for ${file}"
    digest="$(printf '%s\n' "${output}" | awk 'NR == 1 { print $1 }')"
  elif command -v shasum >/dev/null 2>&1; then
    output="$(shasum -a 256 "${file}" 2>/dev/null)" ||
      die "shasum failed for ${file}"
    digest="$(printf '%s\n' "${output}" | awk 'NR == 1 { print $1 }')"
  elif command -v openssl >/dev/null 2>&1; then
    output="$(openssl dgst -sha256 "${file}" 2>/dev/null)" ||
      die "openssl failed for ${file}"
    digest="$(printf '%s\n' "${output}" | awk 'NR == 1 { print $NF }')"
  else
    die "no SHA-256 implementation found (need sha256sum, shasum, or openssl)"
  fi

  [[ "${digest}" =~ ^[0-9A-Fa-f]{64}$ ]] ||
    die "invalid SHA-256 output for ${file}"
  printf '%s\n' "${digest}" | LC_ALL=C tr 'A-F' 'a-f'
}

[[ -n "${SOURCE_DIR}" && -d "${SOURCE_DIR}" ]] ||
  die "missing Byepervisor source directory: ${SOURCE_DIR:-<unset>}"
[[ -n "${OUTPUT_ELF}" ]] || die "missing output ELF path"
[[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] ||
  die "PS5_PAYLOAD_SDK must point to an installed PS5 Payload SDK"
[[ "${HYPERHEN_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
  die "HYPERHEN_JOBS must be a positive integer"

for required in Makefile main.c hyperhen_integration.c \
  break_hv_payload/break_hv.c break_hv_payload/entry.S \
  include/offsets/12_70.h; do
  [[ -f "${SOURCE_DIR}/${required}" ]] ||
    die "vendored dependency is incomplete: ${required}"
done

ACTUAL_1270_OFFSETS_SHA256="$(
  sha256_file "${SOURCE_DIR}/include/offsets/12_70.h"
)"
[[ "${ACTUAL_1270_OFFSETS_SHA256}" == "${HYPERHEN_1270_OFFSETS_SHA256}" ]] ||
  die "firmware 12.70 offsets hash mismatch"

export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"

make -C "${SOURCE_DIR}" -j"${HYPERHEN_JOBS}"

BUILT_ELF="${SOURCE_DIR}/byepervisor_reborn.elf"
[[ -s "${BUILT_ELF}" ]] || die "build completed without ${BUILT_ELF}"

mkdir -p "$(dirname "${OUTPUT_ELF}")"
cp -f "${BUILT_ELF}" "${OUTPUT_ELF}"
printf '%s\n' \
  'HyperHEN hypervisor base: Byepervisor Reborn commit ea04f7132b837087f552c57261cb800481a3a910' \
  "Firmware 12.70 profile overlay: commit 0c353514d90d51d9f1233586b33620355833ae93; offsets SHA-256 ${ACTUAL_1270_OFFSETS_SHA256}" \
  > "${OUTPUT_ELF}.provenance"

printf 'build_hypervisor: wrote %s (%s bytes)\n' \
  "${OUTPUT_ELF}" "$(wc -c < "${OUTPUT_ELF}" | tr -d ' ')"
