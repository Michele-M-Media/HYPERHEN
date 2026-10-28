#!/usr/bin/env bash
# Builds CheatRunner.elf by driving the Makefile (PS5 payload SDK, no CMake).
#
# Usage: ./build-cheatrunner.sh [options]
#   --clean            wipe build-make/ before building
#   --sqlite           force-enable bundled SQLite (make SQLITE=1)
#   --no-sqlite        force-disable SQLite (make SQLITE=0)
#   --skip-js-check    skip the dashboard JS syntax check
#   -j, --jobs N       parallel jobs (default: all cores)
#   -v, --verbose      print the full make output
#   -h, --help         show this help
#
# Flags can be combined: ./build-cheatrunner.sh --clean --sqlite -j 4

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ELF="$ROOT/build-make/CheatRunner.elf"

OPT_CLEAN=0
OPT_JS_CHECK=1
OPT_VERBOSE=0
OPT_SQLITE=""   # "" = auto-detect, "1" = force on, "0" = force off
OPT_JOBS="$(nproc 2>/dev/null || echo 2)"

# ------------------------------------------------------------------ output styling
if [[ -t 1 ]]; then
  C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'
  C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_CYAN=$'\033[36m'
else
  C_RESET=""; C_BOLD=""; C_DIM=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_CYAN=""
fi

fail()    { echo "${C_RED}${C_BOLD}✗ ERROR:${C_RESET} $*" >&2; exit 1; }
warn()    { echo "${C_YELLOW}⚠ WARNING:${C_RESET} $*" >&2; }
info()    { echo "${C_DIM}  ·${C_RESET} $*"; }
ok()      { echo "${C_GREEN}✓${C_RESET} $*"; }
section() { echo; echo "${C_CYAN}${C_BOLD}── $* ──${C_RESET}"; }
usage()   { sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

# ------------------------------------------------------------------ arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)         OPT_CLEAN=1 ;;
    --sqlite)        OPT_SQLITE=1 ;;
    --no-sqlite)     OPT_SQLITE=0 ;;
    --skip-js-check) OPT_JS_CHECK=0 ;;
    -j|--jobs)       shift; OPT_JOBS="${1:-}" ;;
    -j[0-9]*)        OPT_JOBS="${1#-j}" ;;
    -v|--verbose)    OPT_VERBOSE=1 ;;
    -h|--help)       usage; exit 0 ;;
    *)               usage >&2; echo >&2; fail "Unknown argument: $1" ;;
  esac
  shift
done
[[ "$OPT_JOBS" =~ ^[1-9][0-9]*$ ]] || fail "--jobs needs a positive number."

VERSION="$(sed -n 's/^VERSION := *//p' "$ROOT/Makefile" | head -n1)"
echo "${C_BOLD}CheatRunner${C_RESET} ${VERSION:+v$VERSION }build"
BUILD_START=$SECONDS

# ------------------------------------------------------------------ host tools
section "Checking tools"
command -v make    >/dev/null || fail "make is not installed or not in PATH."
command -v python3 >/dev/null || fail "python3 is required to generate the dashboard assets."
command -v cc      >/dev/null || fail "A host C compiler (cc) is required to pre-compress the dashboard."
ok "make, python3, cc"

# ------------------------------------------------------------------ SDK
# Same lookup order as the Makefile: /opt wins, then PS5_PAYLOAD_SDK.
SDK_ROOT=""
for dir in /opt/ps5-payload-sdk "${PS5_PAYLOAD_SDK:-}" "$ROOT/PS5-Projects/sdk-master" "$ROOT/ps5-payload-sdk"; do
  if [[ -n "$dir" && -f "$dir/toolchain/prospero.cmake" ]]; then
    SDK_ROOT="$dir"
    break
  fi
done
[[ -n "$SDK_ROOT" ]] || fail "PS5 payload SDK not found. Install it at /opt/ps5-payload-sdk or set PS5_PAYLOAD_SDK."
[[ -x "$SDK_ROOT/bin/prospero-clang" ]] || fail "prospero-clang is missing in $SDK_ROOT/bin."
for lib in libkernel_sys.so libSceSystemService.so libSceUserService.so; do
  [[ -f "$SDK_ROOT/target/lib/$lib" ]] || fail "SDK library not found: $SDK_ROOT/target/lib/$lib"
done
ok "SDK: $SDK_ROOT"

# ------------------------------------------------------------------ dashboard JS
if [[ "$OPT_JS_CHECK" -eq 1 ]]; then
  if command -v node >/dev/null; then
    section "Dashboard JS check"
    node --check "$ROOT/src/web/app.js" || fail "Dashboard JS has a syntax error (or pass --skip-js-check)."
    ok "src/web/app.js"
  else
    warn "node not found, skipping the dashboard JS check."
  fi
fi

# ------------------------------------------------------------------ clean
if [[ "$OPT_CLEAN" -eq 1 ]]; then
  section "Cleaning"
  rm -rf "$ROOT/build-make"
  ok "build-make/ removed"
fi

# ------------------------------------------------------------------ SQLite
MAKE_ARGS=(-j"$OPT_JOBS")
case "$OPT_SQLITE" in
  1)
    [[ -f "$ROOT/src/third_party/sqlite3.c" && -f "$ROOT/src/third_party/sqlite3.h" ]] ||
      fail "--sqlite needs src/third_party/sqlite3.c and sqlite3.h (https://sqlite.org/download.html)."
    MAKE_ARGS+=(SQLITE=1) ;;
  0) MAKE_ARGS+=(SQLITE=0) ;;
esac

# ------------------------------------------------------------------ build
section "Building (jobs: $OPT_JOBS)"
LOG="$(mktemp -t cheatrunner-build.XXXXXX)"
trap 'rm -f "$LOG"' EXIT

# Shows one status line per step instead of the raw compiler commands.
progress() {
  local n=0 line what tty=0
  [[ -t 1 ]] && tty=1
  while IFS= read -r line; do
    printf '%s\n' "$line" >> "$LOG"
    what=""
    case "$line" in
      *prospero-clang*" -c "*)
        what="${line#* -c }"; what="${what%% *}"; n=$((n + 1)); what="[$n] ${what#src/}" ;;
      *"-o build-make/CheatRunner.elf"*) what="linking CheatRunner.elf" ;;
      *"-o build-make/payload/"*)        what="shell payload" ;;
      *tools/gen_*|*dashboard_bundle*)   what="dashboard assets" ;;
    esac
    if [[ -n "$what" && "$tty" -eq 1 ]]; then
      printf '\r\033[K%s  ·%s %s' "$C_DIM" "$C_RESET" "$what"
    fi
  done
  [[ "$tty" -eq 1 ]] && printf '\r\033[K'
  return 0
}

# A failing make must reach the report below, not trip set -e inside the pipeline.
set +e
if [[ "$OPT_VERBOSE" -eq 1 ]]; then
  make -C "$ROOT" "${MAKE_ARGS[@]}" 2>&1 | tee "$LOG"
else
  make -C "$ROOT" --no-print-directory "${MAKE_ARGS[@]}" 2>&1 | progress
fi
RC=${PIPESTATUS[0]}
set -e

if [[ "$RC" -ne 0 ]]; then
  echo "${C_RED}${C_BOLD}✗ make failed${C_RESET} (last lines of the log):" >&2
  tail -n 25 "$LOG" | cut -c1-240 >&2
  exit 1
fi

COMPILED=$(grep -c ' -c .*\.\(c\|cpp\) -o ' "$LOG" || true)
WARNINGS=$(grep -c 'warning:' "$LOG" || true)
ok "compiled $COMPILED file(s)"
if [[ "$WARNINGS" -gt 0 ]]; then
  warn "$WARNINGS compiler warning(s); run with --verbose to see them."
fi

# ------------------------------------------------------------------ summary
section "Result"
[[ -f "$ELF" ]] || fail "Build finished, but $ELF was not produced."
SIZE_KB=$(( $(wc -c < "$ELF") / 1024 ))
ok "$ELF"
info "size:   ${SIZE_KB} KiB"
command -v sha256sum >/dev/null && info "sha256: $(sha256sum "$ELF" | cut -d' ' -f1)"
info "time:   $(( SECONDS - BUILD_START ))s"
echo
echo "Send it to the console, then open ${C_BOLD}http://<PS5-IP>:9999${C_RESET}"
