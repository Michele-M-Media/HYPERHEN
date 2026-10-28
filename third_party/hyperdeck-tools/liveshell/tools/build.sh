#!/usr/bin/env bash
# Build LiveShell Engine (Linux / WSL).
#   ONIONHEN=~/onionHEN-beta2 PS5_PAYLOAD_SDK=~/ps5-payload-sdk tools/build.sh
# Copies src/ into the OnionHEN v0.9.0-beta2 tree as source/liveshell, builds the "liveshell"
# target and copies liveshell.elf to OUT (default: next to this repo's folder).
set -e

HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/src"
TREE="${ONIONHEN:-$HOME/ps5/wallpaper/onionHEN-beta2}"
SDK="${PS5_PAYLOAD_SDK:-$HOME/ps5/wallpaper/sdk-upstream}"
OUT="${OUT:-$(dirname "$HERE")}"
BUILD="$TREE/build"
DEST="$TREE/source/liveshell"

[ -d "$TREE/source" ] || { echo "OnionHEN tree not found: $TREE (set ONIONHEN=...)"; exit 1; }
[ -d "$SDK" ] || { echo "PS5 payload SDK not found: $SDK (set PS5_PAYLOAD_SDK=...)"; exit 1; }

mkdir -p "$DEST"
rm -f "$DEST"/*.cpp "$DEST"/*.hpp "$DEST"/*.html "$DEST"/icon0.png
grep -q '^add_subdirectory(liveshell)' "$TREE/source/CMakeLists.txt" || echo 'add_subdirectory(liveshell)' >> "$TREE/source/CMakeLists.txt"
cp "$SRC"/*.cpp "$SRC"/*.hpp "$SRC"/*.html "$SRC/CMakeLists.txt" "$DEST/"
cp "$HERE/assets/icon0.png" "$DEST/icon0.png"

[ -d "$HOME/ps5/wallpaper/bin" ] && export PATH="$HOME/ps5/wallpaper/bin:$PATH"
[ -d /usr/lib/llvm-18/bin ] && export PATH="/usr/lib/llvm-18/bin:$PATH" && export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config
export PS5_PAYLOAD_SDK="$SDK"

cmake -S "$TREE/source" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DONIONHEN_KSTUFF_ELF="$TREE/.cache/dependencies/kstuff.elf" -DPS5_PAYLOAD_SDK="$SDK" \
    > /tmp/liveshell_configure.log 2>&1 || { tail -30 /tmp/liveshell_configure.log; exit 1; }

if ! cmake --build "$BUILD" -j"$(nproc)" --target liveshell > /tmp/liveshell_build.log 2>&1; then
    grep -E "error:|FAILED|undefined symbol" /tmp/liveshell_build.log | sort -u | head -30
    exit 1
fi

sha256sum "$BUILD/bin/liveshell.elf"
cp "$BUILD/bin/liveshell.elf" "$OUT/liveshell.elf"
echo "-> $OUT/liveshell.elf"
