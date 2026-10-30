#!/usr/bin/env bash
# Release-compatible 0.9.4 build. Keeps the established engine inputs frozen.
# No dependency downloads, SDK changes, or automatic build-tree deletion.
set -euo pipefail

release_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${release_root}/build-release"
jobs="${JOBS:-6}"
configure_only=0

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
usage() {
  printf '%s\n' \
    'Usage: bash scripts/build_release.sh [--jobs N] [--build-dir PATH] [--configure-only]' \
    'Set PS5_PAYLOAD_SDK to a complete local PS5 Payload SDK installation.'
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --jobs) [[ $# -ge 2 ]] || die '--jobs requires a value'; jobs="$2"; shift 2 ;;
    --build-dir) [[ $# -ge 2 ]] || die '--build-dir requires a value'; build_dir="$2"; shift 2 ;;
    --configure-only) configure_only=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die '--jobs must be a positive integer'
[[ -n "${PS5_PAYLOAD_SDK:-}" ]] || die 'PS5_PAYLOAD_SDK is not set'
[[ -d "$PS5_PAYLOAD_SDK" ]] || die 'PS5_PAYLOAD_SDK is not a directory'
PS5_PAYLOAD_SDK="$(cd -- "$PS5_PAYLOAD_SDK" && pwd)"
export PS5_PAYLOAD_SDK
export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"

for tool in cmake ninja python3 sha256sum cp cmp mkdir; do
  command -v "$tool" >/dev/null 2>&1 || die "missing host tool: $tool"
done
if ! command -v lzma >/dev/null 2>&1 && ! command -v xz >/dev/null 2>&1; then
  die 'xz or lzma is required to pack the bootstrapper'
fi
for tool in prospero-clang prospero-clang++ prospero-as prospero-lld; do
  [[ -x "${PS5_PAYLOAD_SDK}/bin/$tool" ]] || die "SDK tool missing: $tool"
done
[[ -f "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake" ]] || die 'SDK toolchain/prospero.cmake is missing'
for library in libc++.a libc++abi.a libunwind.a; do
  [[ -f "${PS5_PAYLOAD_SDK}/target/lib/$library" ]] || die "complete SDK required: $library is missing"
done
[[ -d "${PS5_PAYLOAD_SDK}/target/include/c++/v1" ]] || die 'SDK C++ headers are missing'
[[ -f "${release_root}/source/CMakeLists.txt" ]] || die 'source/CMakeLists.txt is missing'
[[ -d "${release_root}/third_party" ]] || die 'third_party is missing'
[[ "$(tr -d '\r\n' < "${release_root}/VERSION")" == '0.9.4' ]] || die 'this helper expects VERSION=0.9.4'

components="${release_root}/release-components"
inputs="${release_root}/release-inputs"
crt="${inputs}/sdk/crt1-r42-1360.o"
for checked_dir in "$components" "$inputs"; do
  [[ -f "${checked_dir}/SHA256SUMS" ]] || die "checksum manifest missing: ${checked_dir}/SHA256SUMS"
  (
    cd -- "$checked_dir"
    sha256sum --check --strict SHA256SUMS
  )
done

# These inputs are intentionally absent from a thin Git checkout. CMake still
# addresses their upstream filenames under third_party/debug-tools. Stage only
# checksum-verified release bytes, and refuse to replace a different local file.
for mapping in \
  ps5-fw-spoof:ps5-fw-spoof \
  np-fake-signin-ps5:np-fake-signin-ps5 \
  nanodns:nanodns \
  chukei:chukei-ps5 \
  zftpd:zftpd-ps5-zhttp-v1.6.0; do
  source_name="${mapping%%:*}"
  destination_name="${mapping#*:}"
  retained="${components}/${source_name}.elf"
  destination="${release_root}/third_party/debug-tools/${destination_name}.elf"
  [[ -f "$retained" ]] || die "retained debug-tool input missing: ${source_name}.elf"
  [[ ! -L "$destination" ]] || die "debug-tool destination is a symlink: $destination"
  if [[ -e "$destination" ]]; then
    [[ -f "$destination" ]] && cmp -s -- "$retained" "$destination" || \
      die "existing debug-tool input differs from the release: $destination"
  else
    mkdir -p -- "$(dirname -- "$destination")"
    cp -- "$retained" "$destination"
  fi
done

# CMake's retained-engine mode deliberately has no generator for these inputs.
# Stage them explicitly so a freshly extracted source package can build.
mkdir -p -- "${build_dir}/vendor/hyperdeck-engines" \
  "${build_dir}/vendor/websrv-http/lib" "${build_dir}/vendor/websrv-http/include"
for engine in game-compressor web-file-manager cheatrunner ps5upload; do
  [[ -f "${inputs}/hyperdeck-engines/${engine}.elf" ]] || die "retained HyperDeck engine missing: $engine"
  cp -f -- "${inputs}/hyperdeck-engines/${engine}.elf" \
    "${build_dir}/vendor/hyperdeck-engines/${engine}.elf"
done
cp -f -- "${inputs}/websrv-http/lib/libmicrohttpd.a" \
  "${build_dir}/vendor/websrv-http/lib/libmicrohttpd.a"
cp -f -- "${inputs}/websrv-http/include/microhttpd.h" \
  "${build_dir}/vendor/websrv-http/include/microhttpd.h"

cmake -S "${release_root}/source" -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake" \
  -DPS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK" \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_EXE_LINKER_FLAGS=--start-no-unused-arguments -nostartfiles --end-no-unused-arguments" \
  "-DCMAKE_C_STANDARD_LIBRARIES=${crt}" \
  "-DCMAKE_CXX_STANDARD_LIBRARIES=${crt}" \
  -DHYPERHEN_RELEASE_COMPONENT_DIR="$components" \
  -DHYPERHEN_SOURCE_PLUGIN_RUNTIME=ON \
  -DHYPERHEN_STABLE_SHELLUI_ELF= \
  -DHYPERHEN_INTEGRATED_HYPER=ON \
  -DHYPER_ASSISTANT_ELF="${components}/hyper_assistant_r45.elf" \
  -DHYPER_VOICES_ZIP="${inputs}/hyper/hyper-voices.zip" \
  -DHYPERHEN_P5_STABLE_COMPONENT_DIR="$components" \
  -DHYPERHEN_STABLE_INNER_LOADER_ELF="${components}/hyperhen_elfldr_inner.elf" \
  -DHYPERHEN_EXPERIMENTAL_HYPERSHELL=ON \
  -DHYPERHEN_P5_REUSE_STABLE_HYPERDECK=ON \
  -DHYPERHEN_STABLE_HYPERDECK_ELF= \
  -DHYPERHEN_STABLE_PKG_HELPER_ELF="${components}/hyperdeck-pkg-helper.elf" \
  -DHYPERHEN_STABLE_HYPERSHELL_ELF="${components}/liveshell.elf" \
  -DHYPERHEN_CORE_940_ELF="${components}/hypercore_940.elf" \
  -DHYPERHEN_CORE_1270_ELF="${components}/hypercore_1270.elf" \
  -DHYPERHEN_HYPERVISOR_1270_ELF="${components}/hypervisor_1270.elf" \
  -DHYPERHEN_HYPERVISOR_940_ELF="${components}/hypervisor_940.elf" \
  -DHYPERHEN_LINUX_1360_ELF="${components}/hyperlinux_1360.elf" \
  -DHYPERHEN_CORE_13XX_V092_ELF="${components}/hypercore_13xx_v092.elf"

if [[ "$configure_only" -eq 0 ]]; then
  cmake --build "$build_dir" --target HyperHEN --parallel "$jobs"
  printf 'Built: %s/bin/HyperHEN.elf\n' "$build_dir"
fi
