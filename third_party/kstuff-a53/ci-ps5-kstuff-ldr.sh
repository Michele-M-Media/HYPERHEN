#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

git_safe() {
    git -c core.fsmonitor=false "$@"
}

repair_git_symlinks() {
    local meta path target current repaired=0

    while IFS=$'\t' read -r meta path; do
        [ -n "${path:-}" ] || continue

        target="$(git_safe cat-file -p "${meta#120000 blob }")"

        if [ -L "$path" ]; then
            continue
        fi

        if [ -f "$path" ]; then
            current="$(tr -d '\r' < "$path")"
            if [ "$current" = "$target" ]; then
                rm -f "$path"
                ln -s "$target" "$path"
                repaired=$((repaired + 1))
                continue
            fi
        fi

        printf 'error: %s should be a symlink to %s\n' "$path" "$target" >&2
        printf 'hint: re-checkout the repository with symlink support enabled.\n' >&2
        return 1
    done < <(git_safe ls-tree -r --full-tree HEAD | awk '$1 == 120000 { print $1 " " $2 " " $3 "\t" $4 }')

    if [ "$repaired" -gt 0 ]; then
        printf 'Repaired %d git symlink(s) in the working tree.\n' "$repaired" >&2
    fi
}

git_safe submodule update --init --recursive
repair_git_symlinks

if [ -z "${PS5_PAYLOAD_SDK:-}" ] && [ -d /opt/ps5-payload-sdk ]; then
    export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
fi

if [ -z "${PS5_PAYLOAD_SDK:-}" ] || [ ! -d "$PS5_PAYLOAD_SDK" ]; then
    printf 'error: PS5_PAYLOAD_SDK is not set to a valid SDK path.\n' >&2
    printf 'hint: install the SDK to /opt/ps5-payload-sdk or export PS5_PAYLOAD_SDK explicitly.\n' >&2
    exit 1
fi

kstuff_obs="${KSTUFF_OBS:-0}"
case "$kstuff_obs" in
    0|1)
        ;;
    *)
        printf 'error: KSTUFF_OBS must be 0 or 1, got %s\n' "$kstuff_obs" >&2
        exit 1
        ;;
esac

if [ "$kstuff_obs" = 1 ]; then
    printf 'Building ps5-kstuff with observability enabled.\n' >&2
    kstuff_make_args=(KSTUFF_OBS=1 payload.bin debug-reader.elf debug-reader.bin)
else
    printf 'Building standard ps5-kstuff payload.\n' >&2
    kstuff_make_args=(KSTUFF_OBS=0)
fi

make -C lib clean
make -C prosper0gdb clean
make -C ps5-kstuff clean
make -C ps5-kstuff-ldr clean

make -C ps5-kstuff "${kstuff_make_args[@]}"
make -C ps5-kstuff-ldr KSTUFF_OBS="$kstuff_obs"
