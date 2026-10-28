#!/usr/bin/env bash
# Validate the source-pinned dependencies used by HyperHEN.
#
# HyperHEN does not download a prebuilt kstuff ELF.  The complete HyperCore
# input is kept below third_party/kstuff-a53 and built by CMake.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="${ROOT}/third_party/kstuff-a53"
HYPERVISOR="${ROOT}/third_party/byepervisor-reborn"
HYPERVISOR_5XX="${ROOT}/third_party/bypervisor-5xx"
LEGACY_HEN="${ROOT}/third_party/ps5-hen-dev"
AUTOMOUNT="${ROOT}/third_party/shadowmountplus"
HYPERDPI="${ROOT}/third_party/hyperdpi"
PLUGIN_SDK="${ROOT}/third_party/hyperhen-plugin-sdk"
HYPERLINUX_1360="${ROOT}/third_party/hyperlinux-1360/1360linux.elf"
HYPERVISOR_940="${ROOT}/third_party/byepervisor-940/byepervisor-940.elf"

die() {
  printf 'sync_dependencies: %s\n' "$*" >&2
  exit 1
}

sha256_file() {
  local file="$1"
  local output
  local digest

  if command -v sha256sum >/dev/null 2>&1; then
    output="$(sha256sum "$file" 2>/dev/null)" ||
      die "sha256sum failed for ${file}"
    digest="$(printf '%s\n' "${output}" | awk 'NR == 1 { print $1 }')"
  elif command -v shasum >/dev/null 2>&1; then
    output="$(shasum -a 256 "$file" 2>/dev/null)" ||
      die "shasum failed for ${file}"
    digest="$(printf '%s\n' "${output}" | awk 'NR == 1 { print $1 }')"
  elif command -v openssl >/dev/null 2>&1; then
    output="$(openssl dgst -sha256 "$file" 2>/dev/null)" ||
      die "openssl failed for ${file}"
    digest="$(printf '%s\n' "${output}" | awk 'NR == 1 { print $NF }')"
  else
    die "no SHA-256 implementation found (need sha256sum, shasum, or openssl)"
  fi

  [[ "${digest}" =~ ^[0-9A-Fa-f]{64}$ ]] ||
    die "invalid SHA-256 output for ${file}"
  printf '%s\n' "${digest}" | LC_ALL=C tr 'A-F' 'a-f'
}

[[ $# -eq 0 ]] || die "this source-only dependency check takes no options"
[[ -f "${CORE}/LICENSE" ]] || die "missing vendored kstuff-a53 source"
[[ -f "${CORE}/ps5-kstuff/Makefile" ]] || die "missing ps5-kstuff sources"
[[ -f "${CORE}/ps5-kstuff-ldr/Makefile" ]] ||
  die "missing ps5-kstuff-ldr sources"

for required in \
  "ps5-kstuff/BearSSL/Makefile" \
  "ps5-kstuff/libtomcrypt/makefile" \
  "ps5-kstuff/isa-l_crypto/include/isa-l_crypto/aes_cbc.h"; do
  [[ -f "${CORE}/${required}" ]] ||
    die "incomplete vendored dependency: third_party/kstuff-a53/${required}"
done

[[ -f "${HYPERVISOR}/Makefile" ]] ||
  die "missing vendored Byepervisor Reborn source"
[[ -f "${HYPERVISOR}/break_hv_payload/break_hv.c" ]] ||
  die "incomplete vendored Byepervisor supervisor payload"
[[ -f "${HYPERVISOR}/hyperhen_integration.c" ]] ||
  die "missing HyperHEN hypervisor integration overlay"
[[ -f "${HYPERVISOR}/include/offsets/12_70.h" ]] ||
  die "missing Byepervisor firmware 12.70 profile"
HYPERVISOR_1270_SHA256="$(sha256_file "${HYPERVISOR}/include/offsets/12_70.h")"
[[ "${HYPERVISOR_1270_SHA256}" == \
   "8768441ab5023a391820cae2b56dcf14a0470f05ceb6404ed43015c15f42e007" ]] ||
  die "Byepervisor firmware 12.70 profile hash mismatch"
[[ -f "${HYPERVISOR_5XX}/bypervisor5XX.c" ]] ||
  die "missing vendored Byepervisor 5.xx source"
[[ -f "${HYPERVISOR_5XX}/offsets.h" ]] ||
  die "missing Byepervisor 5.xx profiles"
[[ -f "${HYPERVISOR_5XX}/hyperhen_integration.h" ]] ||
  die "missing HyperHEN Byepervisor 5.xx integration overlay"
[[ -f "${LEGACY_HEN}/main.cpp" ]] ||
  die "missing vendored legacy HEN source"
[[ -f "${LEGACY_HEN}/offsets.h" ]] ||
  die "missing legacy HEN firmware profiles"
[[ -f "${AUTOMOUNT}/Makefile" ]] ||
  die "missing vendored ShadowMountPlus source"
[[ -f "${AUTOMOUNT}/src/main.c" ]] ||
  die "incomplete ShadowMountPlus runtime source"
[[ -f "${AUTOMOUNT}/src/sm_scanner.c" ]] ||
  die "incomplete ShadowMountPlus scanner source"
[[ -f "${AUTOMOUNT}/src/sm_shellcore_offsets.inc" ]] ||
  die "missing ShadowMountPlus firmware profiles"
[[ -f "${AUTOMOUNT}/LICENSE" ]] ||
  die "missing ShadowMountPlus license"
[[ -f "${HYPERDPI}/source/main.c" ]] ||
  die "missing vendored HyperDPI source"
[[ -f "${HYPERDPI}/source/hyperdpi_service.c" ]] ||
  die "incomplete HyperDPI service source"
[[ -f "${HYPERDPI}/webui/dist/index.html" ]] ||
  die "missing embedded HyperDPI WebUI bundle"
[[ -f "${HYPERDPI}/LICENSE" ]] ||
  die "missing HyperDPI GPL license"
[[ -f "${PLUGIN_SDK}/runtime/client.c" ]] ||
  die "missing vendored HyperHEN plugin client runtime"
[[ -f "${PLUGIN_SDK}/include/onion/ipc.h" ]] ||
  die "incomplete HyperHEN plugin compatibility headers"
if ! rg -q '/system_tmp/hyperhen/ipc/plugin_service' \
    "${PLUGIN_SDK}/include/onion/ipc.h"; then
  die "HyperHEN plugin client socket path is not integrated"
fi
[[ -f "${HYPERLINUX_1360}" ]] ||
  die "missing dedicated HyperLinux 13.60 ELF"
HYPERLINUX_1360_SHA256="$(sha256_file "${HYPERLINUX_1360}")"
[[ "${HYPERLINUX_1360_SHA256}" == \
   "e0bdd37ae0367b2864fd2486bcd77e2e930f7d89f2ef96fd235bb15d22db90c7" ]] ||
  die "HyperLinux 13.60 ELF hash mismatch"
[[ -f "${HYPERVISOR_940}" ]] ||
  die "missing dedicated Byepervisor 9.40 ELF"
HYPERVISOR_940_SHA256="$(sha256_file "${HYPERVISOR_940}")"
[[ "${HYPERVISOR_940_SHA256}" == \
   "7b592477a27524ae8fcfc491024aaf3befc5d7214596170d63aa43340a74e688" ]] ||
  die "Byepervisor 9.40 ELF hash mismatch"

printf '%s\n' \
  '[ok] HyperCore sources are local and complete' \
  '     kstuff-a53: 0a69ea31759f048d24b38bfaa23dbec1fd065916' \
  '     no prebuilt kstuff 1.11 ELF will be downloaded or embedded' \
  '[ok] Hypervisor sources are local and complete' \
  '     Byepervisor Reborn: ea04f7132b837087f552c57261cb800481a3a910' \
  '     12.70 profile overlay: 0c353514d90d51d9f1233586b33620355833ae93; offsets 8768441ab5023a391820cae2b56dcf14a0470f05ceb6404ed43015c15f42e007' \
  '     Byepervisor 5.xx: 9c182a1622dd43ea56641a7080638af3673a723f' \
  '     ps5-hen: 699870d16ca4cd5cd70fc88f8cf61b3953d4a292' \
  '     no external 12.70/13.xx hypervisor ELF is required' \
  '[ok] HyperHEN Automount source is local and complete' \
  '     ShadowMountPlus: 98c524890176c7b474fcb03387bec88c4a559861' \
  '     no external automount ELF is required' \
  '[ok] HyperDPI source and client runtime are local and complete' \
  '     HyperDPI: 4e80389a87dd709f5f7ba316cbaa23d05a3cafc3' \
  '     Plugin SDK: 0cf4346536705fb271ba0eb2226d97ebcb23559c' \
  '     no external DPIV00001.elf is required' \
  '[ok] Dedicated HyperLinux 13.60 image is pinned and unchanged' \
  '[ok] Dedicated Byepervisor 9.40 image is pinned and unchanged'
