#!/usr/bin/env bash
# Build the embedded HyperCore payload from the vendored kstuff-a53 sources.
# This wrapper deliberately never downloads a release ELF.

set -euo pipefail

SOURCE_DIR="${1:-}"
OUTPUT_ELF="${2:-}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
KSTUFF_OBS="${KSTUFF_OBS:-0}"
HYPERHEN_JOBS="${HYPERHEN_JOBS:-4}"

die() {
  printf 'build_hypercore: %s\n' "$*" >&2
  exit 1
}

[[ -n "${SOURCE_DIR}" && -d "${SOURCE_DIR}" ]] ||
  die "missing kstuff-a53 source directory: ${SOURCE_DIR:-<unset>}"
[[ -n "${OUTPUT_ELF}" ]] || die "missing output ELF path"
[[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] ||
  die "PS5_PAYLOAD_SDK must point to an installed PS5 Payload SDK"
[[ "${KSTUFF_OBS}" == 0 || "${KSTUFF_OBS}" == 1 ]] ||
  die "KSTUFF_OBS must be 0 or 1"
[[ "${HYPERHEN_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
  die "HYPERHEN_JOBS must be a positive integer"

for required in \
  "ps5-kstuff/BearSSL/Makefile" \
  "ps5-kstuff/libtomcrypt/makefile" \
  "ps5-kstuff/isa-l_crypto/include/isa-l_crypto/aes_cbc.h"; do
  [[ -f "${SOURCE_DIR}/${required}" ]] ||
    die "vendored dependency is incomplete: ${required}"
done

export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"

make -C "${SOURCE_DIR}/ps5-kstuff" -j"${HYPERHEN_JOBS}" \
  KSTUFF_OBS="${KSTUFF_OBS}"
make -C "${SOURCE_DIR}/ps5-kstuff-ldr" -j"${HYPERHEN_JOBS}" \
  KSTUFF_OBS="${KSTUFF_OBS}"

BUILT_ELF="${SOURCE_DIR}/ps5-kstuff-ldr/kstuff.elf"
[[ -s "${BUILT_ELF}" ]] || die "build completed without ${BUILT_ELF}"

mkdir -p "$(dirname "${OUTPUT_ELF}")"
cp -f "${BUILT_ELF}" "${OUTPUT_ELF}"
printf 'HyperCore source: kstuff-a53 commit 0a69ea31759f048d24b38bfaa23dbec1fd065916\n' \
  > "${OUTPUT_ELF}.provenance"

printf 'build_hypercore: wrote %s (%s bytes)\n' \
  "${OUTPUT_ELF}" "$(wc -c < "${OUTPUT_ELF}" | tr -d ' ')"
