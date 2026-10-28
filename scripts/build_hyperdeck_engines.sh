#!/usr/bin/env bash
# Build the complete native web tools embedded by HyperDeck.

set -euo pipefail

SOURCE_ROOT="${1:-}"
OUTPUT_DIR="${2:-}"
MHD_PREFIX="${3:-}"
CMAKE_BIN="${4:-cmake}"
PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
LLVM_CONFIG="${LLVM_CONFIG:-}"
HYPERHEN_JOBS="${HYPERHEN_JOBS:-4}"

die() {
  printf 'build_hyperdeck_engines: %s\n' "$*" >&2
  exit 1
}

[[ -n "${SOURCE_ROOT}" && -d "${SOURCE_ROOT}" ]] ||
  die "missing HyperDeck tool source root"
[[ -n "${OUTPUT_DIR}" ]] || die "missing output directory"
if [[ "${HYPERHEN_PRESERVE_DRIVE_ENGINES:-0}" == 1 ]]; then
  for engine in \
    game-compressor.elf web-file-manager.elf cheatrunner.elf ps5upload.elf; do
    [[ -s "${OUTPUT_DIR}/${engine}" ]] ||
      die "missing prebuilt Drive engine: ${OUTPUT_DIR}/${engine}"
  done
  python3 "$(dirname "$0")/verify_payload_elf.py" check \
    "${OUTPUT_DIR}/game-compressor.elf" \
    "${OUTPUT_DIR}/web-file-manager.elf" \
    "${OUTPUT_DIR}/cheatrunner.elf" \
    "${OUTPUT_DIR}/ps5upload.elf"
  printf 'build_hyperdeck_engines: preserved verified Drive engines in %s\n' \
    "${OUTPUT_DIR}"
  exit 0
fi
[[ -n "${MHD_PREFIX}" && -f "${MHD_PREFIX}/lib/libmicrohttpd.a" &&
   -f "${MHD_PREFIX}/include/microhttpd.h" ]] ||
  die "missing source-built libmicrohttpd prefix"
[[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] ||
  die "PS5_PAYLOAD_SDK must point to an installed PS5 Payload SDK"
[[ -n "${LLVM_CONFIG}" && -x "${LLVM_CONFIG}" ]] ||
  die "LLVM_CONFIG must point to an executable llvm-config"
[[ -x "${CMAKE_BIN}" ]] || die "CMake executable not found: ${CMAKE_BIN}"
[[ "${HYPERHEN_JOBS}" =~ ^[1-9][0-9]*$ ]] ||
  die "HYPERHEN_JOBS must be a positive integer"

for required in \
  game-compressor/Makefile \
  web-file-manager/Makefile \
  web-file-manager/vendor/libsmb2/HYPERHEN_VENDOR_LOCK.md \
  cheatrunner/Makefile \
  ps5upload/payload/Makefile \
  ps5upload/payload/third_party/sqlite3/sqlite3.c \
  ps5upload/hyperhen/generated/webui_page.c; do
  [[ -f "${SOURCE_ROOT}/${required}" ]] ||
    die "vendored tool source is incomplete: ${required}"
done

OUTPUT_DIR="$(realpath -m "${OUTPUT_DIR}")"
SOURCE_ROOT="$(realpath "${SOURCE_ROOT}")"
MHD_PREFIX="$(realpath "${MHD_PREFIX}")"
WORK_DIR="${OUTPUT_DIR}/.hyperdeck-engine-build"
[[ "${OUTPUT_DIR}" != / && "${WORK_DIR}" == "${OUTPUT_DIR}/.hyperdeck-engine-build" ]] ||
  die "refusing unsafe work directory"

mkdir -p "${OUTPUT_DIR}"
rm -rf -- "${WORK_DIR}"
mkdir -p "${WORK_DIR}"
trap 'rm -rf -- "${WORK_DIR}"' EXIT

cp -a "${SOURCE_ROOT}/game-compressor" "${WORK_DIR}/game-compressor"
cp -a "${SOURCE_ROOT}/web-file-manager" "${WORK_DIR}/web-file-manager"
cp -a "${SOURCE_ROOT}/cheatrunner" "${WORK_DIR}/cheatrunner"
cp -a "${SOURCE_ROOT}/ps5upload" "${WORK_DIR}/ps5upload"

LLVM_BINDIR="$(dirname "${LLVM_CONFIG}")"
ZLIB_INCLUDE="${PS5_PAYLOAD_SDK}/target/user/homebrew/include"
ZLIB_LIB="${PS5_PAYLOAD_SDK}/target/user/homebrew/lib/libz.a"
[[ -f "${ZLIB_LIB}" ]] || die "SDK zlib archive is missing"

make -C "${WORK_DIR}/game-compressor" clean \
  PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}" LLVM_CONFIG="${LLVM_CONFIG}" \
  LLVM_BINDIR="${LLVM_BINDIR}"
make -C "${WORK_DIR}/game-compressor" -j"${HYPERHEN_JOBS}" \
  HYPERHEN_NO_MEDIA_TILE=1 BUILD_VERSION=1.0.4 \
  PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}" LLVM_CONFIG="${LLVM_CONFIG}" \
  LLVM_BINDIR="${LLVM_BINDIR}" ZLIB_INCLUDE="${ZLIB_INCLUDE}" \
  ZLIB_LIB="${ZLIB_LIB}"

chmod 0755 "${WORK_DIR}/web-file-manager/hyperhen/prospero-pkg-config"
make -C "${WORK_DIR}/web-file-manager" clean
make -C "${WORK_DIR}/web-file-manager" -j"${HYPERHEN_JOBS}" \
  HYPERHEN_NO_MEDIA_TILE=1 PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}" \
  LLVM_CONFIG="${LLVM_CONFIG}" CMAKE="${CMAKE_BIN}" \
  PKG_CONFIG="${WORK_DIR}/web-file-manager/hyperhen/prospero-pkg-config" \
  HYPERHEN_MHD_PREFIX="${MHD_PREFIX}"

make -C "${WORK_DIR}/cheatrunner" clean \
  PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}"
make -C "${WORK_DIR}/cheatrunner" -j"${HYPERHEN_JOBS}" \
  PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}" BUILD_ID=hyperhen-embedded

make -C "${WORK_DIR}/ps5upload/payload" clean \
  PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}" LLVM_CONFIG="${LLVM_CONFIG}"
mkdir -p "${WORK_DIR}/ps5upload/payload/build"
cp -f "${WORK_DIR}/ps5upload/hyperhen/generated/"*.c \
  "${WORK_DIR}/ps5upload/payload/build/"
touch "${WORK_DIR}/ps5upload/payload/build/"*.c
PATH="$(dirname "$0"):${PATH}" \
  make -C "${WORK_DIR}/ps5upload/payload" -j"${HYPERHEN_JOBS}" \
  HYPERHEN_NO_MEDIA_TILE=1 PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK}" \
  LLVM_CONFIG="${LLVM_CONFIG}"

cp -f "${WORK_DIR}/game-compressor/game-compressor.elf" \
  "${OUTPUT_DIR}/game-compressor.elf"
cp -f "${WORK_DIR}/web-file-manager/web-file-mgr.elf" \
  "${OUTPUT_DIR}/web-file-manager.elf"
cp -f "${WORK_DIR}/cheatrunner/build-make/CheatRunner.elf" \
  "${OUTPUT_DIR}/cheatrunner.elf"
cp -f "${WORK_DIR}/ps5upload/payload/ps5upload.elf" \
  "${OUTPUT_DIR}/ps5upload.elf"

python3 "$(dirname "$0")/verify_payload_elf.py" check \
  "${OUTPUT_DIR}/game-compressor.elf" \
  "${OUTPUT_DIR}/web-file-manager.elf" \
  "${OUTPUT_DIR}/cheatrunner.elf" \
  "${OUTPUT_DIR}/ps5upload.elf"

printf 'build_hyperdeck_engines: wrote complete native engines to %s\n' \
  "${OUTPUT_DIR}"
