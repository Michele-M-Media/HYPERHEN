#!/usr/bin/env bash
set -euo pipefail
archive=$(realpath "$1")
destination=$(realpath -m "$2")
mkdir -p "$destination/include" "$destination/lib" "$destination/work"
if [[ "${3:-ps5}" == ps5 ]]; then
  : "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}"
  source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"
  cross_args=(--host=x86_64-pc-freebsd)
else
  cross_args=()
fi
tar -xzf "$archive" -C "$destination/work"
cd "$destination/work/libmicrohttpd-1.0.1"
CFLAGS='-O1 -fPIC' ./configure "${cross_args[@]}" --disable-shared --enable-static \
  --disable-https --disable-curl --disable-examples --disable-tools --disable-doc \
  > "$destination/configure.log" 2>&1
make -j4 > "$destination/make.log" 2>&1
cp src/microhttpd/.libs/libmicrohttpd.a "$destination/lib/"
cp src/include/microhttpd.h "$destination/include/"
