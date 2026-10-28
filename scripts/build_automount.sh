#!/usr/bin/env bash
# Build HyperHEN Automount from the user-supplied ShadowMountPlus beta9 source.

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
  VERSION_TAG="1.7-beta9-hyperhen" shadowmountplus.elf

BUILT_ELF="${SOURCE_DIR}/shadowmountplus.elf"
[[ -s "${BUILT_ELF}" ]] || die "build completed without ${BUILT_ELF}"

mkdir -p "$(dirname "${OUTPUT_ELF}")"
cp -f "${BUILT_ELF}" "${OUTPUT_ELF}"
printf '%s\n' \
  'HyperHEN Automount source: user-supplied ShadowMountPlus-1.7beta9.zip' \
  'Source ZIP SHA256: 0bf0319e26a53a6c7b58f036a6f051761036c1e3bec7df8da87d5852d9489656' \
  > "${OUTPUT_ELF}.provenance"

printf 'build_automount: wrote %s (%s bytes)\n' \
  "${OUTPUT_ELF}" "$(wc -c < "${OUTPUT_ELF}" | tr -d ' ')"
