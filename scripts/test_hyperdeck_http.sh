#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_build="$project_dir/build-hyperdeck/host-http"
if [[ ! -f "$test_build/lib/libmicrohttpd.a" ]]; then
  bash "$project_dir/scripts/build_websrv_http.sh" "$project_dir/third_party/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz" "$test_build" host
fi
python3 "$project_dir/scripts/embed_hyperdeck.py" --assets "$project_dir/source/hyperdeck/assets" \
  --logo "$project_dir/assets/logo.png" --output "$test_build/assets.c"
cc -I"$test_build/include" -I"$project_dir/third_party/websrv/src" \
  -c "$project_dir/third_party/websrv/src/asset.c" -o "$test_build/asset.o"
cc -c "$test_build/assets.c" -o "$test_build/assets.o"
c++ -std=c++20 -O1 -Wall -Wextra -Werror -Wno-misleading-indentation \
  -I"$test_build/include" -I"$project_dir/third_party/websrv/src" \
  -I"$project_dir/source/hyperdeck/include" -I"$project_dir/source/include" \
  "$project_dir/source/hyperdeck/server.cpp" "$project_dir/source/hyperdeck/json.cpp" \
  "$project_dir/source/hyperdeck/pkg_catalog.cpp" "$project_dir/tools/pkg_portal_host.cpp" \
  "$project_dir/tools/hyperdeck_host_backend.cpp" "$project_dir/tools/hyperdeck_engine_host.cpp" \
  "$test_build/asset.o" "$test_build/assets.o" \
  "$test_build/lib/libmicrohttpd.a" -pthread -o "$test_build/hyperdeck-host"
node "$project_dir/tools/check_hyperdeck_http.cjs" "$test_build/hyperdeck-host"
node "$project_dir/tools/check_hyperdeck_selector.cjs" "$test_build/hyperdeck-host"
node "$project_dir/tools/check_selector_ui_compat.cjs" "$project_dir/source/hyperdeck/assets/selector-5xx-r10-20261025.js"
bash "$project_dir/tools/check_hyperdeck_selector.sh" "$test_build/hyperdeck-host"
