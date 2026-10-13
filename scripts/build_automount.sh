#!/usr/bin/env bash
# Build the embedded HyperHEN Automount payload from vendored ShadowMountPlus.

set -euo pipefail

SOURCE_DIR="${1:-}"
OUTPUT_ELF="${2:-}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
HYPERHEN_JOBS="${HYPERHEN_JOBS:-4}"

die() {
  printf 'build_automount: %s\n' "$*" >&2
  exit 1
}

[[ -n "${SOURCE_DIR}" && -d "${SOURCE_DIR}" ]] ||
  die "missing ShadowMountPlus source directory: ${SOURCE_DIR:-<unset>}"
[[ -n "${OUTPUT_ELF}" ]] || die "missing output ELF path"
[[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] ||
  die "PS5_PAYLOAD_SDK must point to an installed PS5 Payload SDK"
[[ "${HYPERHEN_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
  die "HYPERHEN_JOBS must be a positive integer"

for required in Makefile LICENSE src/main.c src/sm_scanner.c \
  src/sm_shellcore_offsets.inc include/sm_runtime.h config.ini.example; do
  [[ -f "${SOURCE_DIR}/${required}" ]] ||
    die "vendored dependency is incomplete: ${required}"
done

export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"

make -C "${SOURCE_DIR}" clean
make -C "${SOURCE_DIR}" -j"${HYPERHEN_JOBS}" \
  VERSION_TAG="1.7-beta5fix1-hyperhen" shadowmountplus.elf

BUILT_ELF="${SOURCE_DIR}/shadowmountplus.elf"
[[ -s "${BUILT_ELF}" ]] || die "build completed without ${BUILT_ELF}"

mkdir -p "$(dirname "${OUTPUT_ELF}")"
cp -f "${BUILT_ELF}" "${OUTPUT_ELF}"
printf '%s\n' \
  'HyperHEN Automount source: ShadowMountPlus 1.7-beta5fix1, snapshot 98c524890176c7b474fcb03387bec88c4a559861' \
  > "${OUTPUT_ELF}.provenance"

printf 'build_automount: wrote %s (%s bytes)\n' \
  "${OUTPUT_ELF}" "$(wc -c < "${OUTPUT_ELF}" | tr -d ' ')"
