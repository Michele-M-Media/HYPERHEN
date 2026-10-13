#!/usr/bin/env bash
# Build the embedded HyperLinux payload from the vendored pLinux5-NG source.

set -euo pipefail

SOURCE_DIR="${1:-}"
OUTPUT_ELF="${2:-}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
HYPERHEN_JOBS="${HYPERHEN_JOBS:-4}"

die() {
  printf 'build_hyperlinux: %s\n' "$*" >&2
  exit 1
}

[[ -n "${SOURCE_DIR}" && -d "${SOURCE_DIR}" ]] ||
  die "missing HyperLinux source directory: ${SOURCE_DIR:-<unset>}"
[[ -n "${OUTPUT_ELF}" ]] || die "missing output ELF path"
[[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] ||
  die "PS5_PAYLOAD_SDK must point to an installed PS5 Payload SDK"
[[ "${HYPERHEN_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
  die "HYPERHEN_JOBS must be a positive integer"

for required in Makefile LICENSE source/main.c source/loader.c \
  shellcode_hv/Makefile shellcode_kernel/Makefile; do
  [[ -f "${SOURCE_DIR}/${required}" ]] ||
    die "vendored dependency is incomplete: ${required}"
done

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hyperlinux-build.XXXXXX")"
cleanup() {
  rm -rf -- "${BUILD_DIR}"
}
trap cleanup EXIT

cp -a "${SOURCE_DIR}/." "${BUILD_DIR}/"
export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"
make -C "${BUILD_DIR}" -j"${HYPERHEN_JOBS}"

BUILT_ELF="${BUILD_DIR}/bin/pLinux5-NG.elf"
[[ -s "${BUILT_ELF}" ]] || die "build completed without ${BUILT_ELF}"

mkdir -p "$(dirname "${OUTPUT_ELF}")"
cp -f "${BUILT_ELF}" "${OUTPUT_ELF}"
printf '%s\n' \
  'HyperLinux source: vendored pLinux5-NG snapshot; see third_party/hyperlinux/HYPERHEN_VENDOR_LOCK.md' \
  > "${OUTPUT_ELF}.provenance"

printf 'build_hyperlinux: wrote %s (%s bytes)\n' \
  "${OUTPUT_ELF}" "$(wc -c < "${OUTPUT_ELF}" | tr -d ' ')"
