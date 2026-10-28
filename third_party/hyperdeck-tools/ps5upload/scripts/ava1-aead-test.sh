#!/usr/bin/env bash
#
# Builds and runs payload/ava1/test/aead_test.c (RFC 8439 vectors, Monocypher
# differential, throughput) for `make test-ava1`:
#
#   - natively: the AVX2 path on an x86-64 host, the portable path elsewhere;
#   - on an arm64 Mac, also as x86-64 under Rosetta 2 (which runs AVX2), so the
#     console's ChaCha20 path is exercised on the machine the payload is
#     developed on. Its MB/s are translated code: indicative only.
#
# Without Rosetta 2 the x86-64 run is skipped with a notice, never silently. Set
# AVA1_X86_IMAGE to a linux/amd64 Docker image that has gcc (or cc) to run it
# there instead; Docker Desktop on Apple Silicon also translates with Rosetta.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${AVA1_AEAD_OUT:-$root/engine/target/ava1-aead}"
cc="${CC:-cc}"
mkdir -p "$out"

ava1="payload/ava1"
mono="payload/third_party/monocypher"
warn=(-O2 -Wall -Wextra -Werror)

# build <output> <target arch> <extra cc flags...>: -mavx2 goes to the AVX2 unit
# only, and only for x86-64.
build() {
	local bin="$1" arch="$2" avx=()
	shift 2
	[[ "$arch" == x86_64 ]] && avx=(-mavx2)
	(
		cd "$root"
		"$cc" "$@" "${warn[@]}" -I"$ava1" -c "$ava1/ava1_aead.c" -o "$bin.aead.o"
		"$cc" "$@" "${warn[@]}" ${avx[@]+"${avx[@]}"} -I"$ava1" -c "$ava1/ava1_chacha_avx2.c" -o "$bin.avx2.o"
		"$cc" "$@" -O2 -w -I"$mono" -c "$mono/monocypher.c" -o "$bin.mono.o"
		"$cc" "$@" "${warn[@]}" -I"$ava1" -I"$mono" "$ava1/test/aead_test.c" \
			"$bin.aead.o" "$bin.avx2.o" "$bin.mono.o" -o "$bin"
	)
}

build "$out/aead_test" "$(uname -m)"
"$out/aead_test"

if [[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]]; then
	if [[ -e /Library/Apple/usr/libexec/oah/libRosettaRuntime ]]; then
		build "$out/aead_test_x86_64" x86_64 -arch x86_64
		echo "(x86_64 under Rosetta 2: translated code, MB/s indicative only)"
		"$out/aead_test_x86_64"
		exit 0
	fi
	reason="Rosetta 2 is not installed (softwareupdate --install-rosetta --agree-to-license)"
elif [[ "$(uname -m)" == x86_64 ]]; then
	exit 0 # the native run above was the x86-64 one
else
	reason="this host is $(uname -m), not x86-64"
fi

if [[ -n "${AVA1_X86_IMAGE:-}" ]]; then
	echo "(x86_64 in Docker image $AVA1_X86_IMAGE: $reason)"
	docker run --rm --platform linux/amd64 -v "$root/payload:/payload:ro" "$AVA1_X86_IMAGE" sh -ec '
		cc=$(command -v gcc || command -v cc)
		f="-O2 -Wall -Wextra -Werror -I/payload/ava1"
		$cc $f -c /payload/ava1/ava1_aead.c -o /tmp/a.o
		$cc $f -mavx2 -c /payload/ava1/ava1_chacha_avx2.c -o /tmp/b.o
		$cc -O2 -w -I/payload/third_party/monocypher -c /payload/third_party/monocypher/monocypher.c -o /tmp/m.o
		$cc $f -I/payload/third_party/monocypher /payload/ava1/test/aead_test.c /tmp/a.o /tmp/b.o /tmp/m.o -o /tmp/t
		/tmp/t'
	exit 0
fi

echo "NOTICE: the AVX2 ChaCha20 path was NOT exercised on this host: $reason." >&2
echo "        Set AVA1_X86_IMAGE=<linux/amd64 image with gcc> to run it in Docker; x86-64 CI runs it natively." >&2
