#!/usr/bin/env bash
set -euo pipefail
root="${1:?usage: test_localization.sh /path/to/HyperHEN}"
build="${2:-$root/build-hyperdeck/localization-host}"
mkdir -p "$build"
python3 "$root/source/i18n/generate_catalog.py" --locales-dir "$root/source/i18n" --catalog notifications --output "$build/notify_i18n_catalog.inc"
python3 "$root/source/i18n/generate_catalog.py" --locales-dir "$root/source/i18n" --catalog toolbox --output "$build/toolbox_i18n_catalog.inc"
common=(-DONION_HOST_TEST=1 -I"$root/source/libonion_platform/include" -I"$root/source/shellui/include" -I"$root/source/util/include" -I"$root/source/util/tests" -I"$root/third_party/cjson" -I"$build" -Wall -Wextra)
for source in "$root/source/libonion_platform/source/notify_i18n.c" "$root/source/libonion_platform/source/notify.c" "$root/source/util/source/util_language.c" "$root/source/util/tests/test_harness.c" "$root/source/util/tests/test_platform_notify.c" "$root/source/util/tests/test_util_language.c" "$root/tools/localization_host_check.c"; do
  cc -std=c11 -D_DEFAULT_SOURCE "${common[@]}" -c "$source" -o "$build/$(basename "${source%.c}").o"
done
c++ -std=c++20 "${common[@]}" -c "$root/third_party/cjson/cJSON.cpp" -o "$build/cJSON.o"
c++ -std=c++20 "${common[@]}" -c "$root/source/shellui/src/toolbox_i18n.cpp" -o "$build/toolbox_i18n.o"
c++ -std=c++20 "${common[@]}" -c "$root/source/util/tests/test_toolbox_i18n.cpp" -o "$build/test_toolbox_i18n.o"
c++ "$build/notify_i18n.o" "$build/notify.o" "$build/util_language.o" "$build/test_harness.o" "$build/test_platform_notify.o" "$build/test_util_language.o" "$build/cJSON.o" "$build/localization_host_check.o" "$build/toolbox_i18n.o" "$build/test_toolbox_i18n.o" -o "$build/localization-check"
"$build/localization-check"
