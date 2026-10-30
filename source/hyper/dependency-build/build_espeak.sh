#!/usr/bin/env bash
set -euo pipefail
root="${HYPER_AI_BUILD_ROOT:?Set HYPER_AI_BUILD_ROOT to an isolated directory containing upstream/}"
source_dir="$root/upstream/espeak"
host_build="$root/espeak-build-host"
ps5_build="$root/espeak-build-ps5"
host_install="$root/espeak-install-host"
ps5_install="$root/espeak-install-ps5"
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
  -DENABLE_TESTS=OFF -DUSE_ASYNC=OFF -DUSE_MBROLA=OFF -DUSE_LIBSONIC=OFF
  -DUSE_LIBPCAUDIO=OFF -DUSE_KLATT=OFF -DUSE_SPEECHPLAYER=OFF -DESPEAK_COMPAT=OFF)
cmake -S "$source_dir" -B "$host_build" "${common[@]}" \
  -DCMAKE_INSTALL_PREFIX="$host_install"
cmake --build "$host_build" --parallel 2
cmake --install "$host_build"
mkdir -p "$host_install/lib"
cp "$host_build/src/ucd-tools/libucd.a" "$host_install/lib/libucd.a"
italian_data="$host_install/share/italian/espeak-ng-data"
mkdir -p "$italian_data/lang/roa"
for name in phondata phonindex phontab intonations it_dict; do
  cp "$host_install/share/espeak-ng-data/$name" "$italian_data/$name"
done
cp "$host_install/share/espeak-ng-data/lang/roa/it" "$italian_data/lang/roa/it"

cmake -S "$source_dir" -B "$ps5_build" "${common[@]}" \
  -DCMAKE_TOOLCHAIN_FILE=${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}/toolchain/prospero.cmake \
  -DPS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK} -DCOMPILE_INTONATIONS=OFF \
  -DCMAKE_INSTALL_PREFIX="$ps5_install"
cmake --build "$ps5_build" --target espeak-ng ucd --parallel 2
# Install only static libraries/headers for the worker, never a foreign CLI.
mkdir -p "$ps5_install/lib" "$ps5_install/include"
cp "$ps5_build/src/libespeak-ng/libespeak-ng.a" "$ps5_install/lib/libespeak-ng.a"
cp "$ps5_build/src/ucd-tools/libucd.a" "$ps5_install/lib/libucd.a"
cp -a "$source_dir/src/include/espeak-ng" "$ps5_install/include/"
printf 'eSpeak host data: %s/share/espeak-ng-data\n' "$host_install"
printf 'Minimal Italian data: %s\n' "$italian_data"
printf 'eSpeak PS5 libraries: %s/lib\n' "$ps5_install"
