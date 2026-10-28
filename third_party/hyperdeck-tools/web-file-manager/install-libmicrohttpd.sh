#!/usr/bin/env bash
set -euo pipefail

LIB_VER="${LIBMICROHTTPD_VERSION:-1.0.1}"
LIB_URL="${LIBMICROHTTPD_URL:-https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-${LIB_VER}.tar.gz}"
LIB_TARBALL="${LIBMICROHTTPD_TARBALL:-}"

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
  echo "error: PS5_PAYLOAD_SDK is not set" >&2
  exit 1
fi

source "${PS5_PAYLOAD_SDK}/toolchain/prospero.sh"

if "${PS5_PAYLOAD_SDK}/bin/prospero-pkg-config" --exists libmicrohttpd; then
  echo "libmicrohttpd is already available in PS5_PAYLOAD_SDK"
  exit 0
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf -- "${tmpdir}"' EXIT

archive="${tmpdir}/libmicrohttpd-${LIB_VER}.tar.gz"
if [[ -n "${LIB_TARBALL}" ]]; then
  cp "${LIB_TARBALL}" "${archive}"
else
  if command -v wget >/dev/null 2>&1; then
    wget -O "${archive}" "${LIB_URL}"
  elif command -v curl >/dev/null 2>&1; then
    curl -L -o "${archive}" "${LIB_URL}"
  else
    echo "error: install wget or curl, or set LIBMICROHTTPD_TARBALL" >&2
    exit 1
  fi
fi

tar xf "${archive}" -C "${tmpdir}"
cd "${tmpdir}/libmicrohttpd-${LIB_VER}"

export CFLAGS="${CFLAGS:-} -O1"
./configure --prefix="${PREFIX}" \
  --host=x86_64-pc-freebsd \
  --enable-static \
  --disable-shared \
  --disable-doc \
  --disable-curl \
  --disable-examples

"${MAKE:-make}"
"${MAKE:-make}" install

if [[ -n "${PS5_CROSS_FIX_ROOT:-}" ]]; then
  "${PS5_CROSS_FIX_ROOT}" "${DESTDIR}/${PREFIX}"
fi

"${PS5_PAYLOAD_SDK}/bin/prospero-pkg-config" --exists libmicrohttpd
echo "libmicrohttpd installed for ps5-payload-sdk"
