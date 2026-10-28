#!/usr/bin/env bash
# Host-only HTTP/UI regression build. No console payloads are linked.
set -euo pipefail
root="${1:?usage: build_selector_host.sh /path/to/HyperHEN}"
http="$root/build-hyperdeck/host-http"
build="$root/build-hyperdeck/localization-host"
mkdir -p "$build"
python3 "$root/scripts/embed_hyperdeck.py" --assets "$root/source/hyperdeck/assets" --logo "$root/assets/logo.png" --output "$build/assets.c"
python3 "$root/source/i18n/generate_catalog.py" --locales-dir "$root/source/i18n" --catalog notifications --output "$build/notify_i18n_catalog.inc"
includes=(-D_DEFAULT_SOURCE -I"$root/source/hyperdeck/include" -I"$root/source/libonion_platform/include" -I"$root/source/include" -I"$root/third_party/websrv/src" -I"$http/include" -I"$build")
cc -std=c11 "${includes[@]}" -c "$build/assets.c" -o "$build/assets.o"
cc -std=c11 "${includes[@]}" -c "$root/third_party/websrv/src/asset.c" -o "$build/asset.o"
cc -std=c11 "${includes[@]}" -c "$root/source/libonion_platform/source/notify_i18n.c" -o "$build/notify_i18n.o"
c++ -std=c++20 -O1 -Wall -Wextra -pthread "${includes[@]}" "$root/source/hyperdeck/server.cpp" "$root/tools/hyperdeck_host_backend.cpp" "$root/tools/hyperdeck_engine_host.cpp" "$root/source/hyperdeck/pkg_catalog.cpp" "$root/tools/pkg_portal_host.cpp" "$root/source/hyperdeck/json.cpp" "$build/asset.o" "$build/assets.o" "$build/notify_i18n.o" "$http/lib/libmicrohttpd.a" -o "$build/hyperdeck-host-r18"
