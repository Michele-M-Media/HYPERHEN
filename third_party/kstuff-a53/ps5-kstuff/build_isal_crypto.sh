#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
src_dir="$repo_root/isa-l_crypto"
build_dir="$src_dir/bin/ps5-zen2-minimal"
lib_path="$src_dir/bin/isa-l_crypto.a"
wrapper_src="$repo_root/isa_l_crypto_ps5_minimal.c"

CC="${CC:-gcc}"
AS="${AS:-nasm}"
AR="${AR:-ar}"
STRIP="${STRIP:-strip}"

cflags=(
  -O3
  -fPIC
  -ffunction-sections
  -fdata-sections
  -fno-stack-protector
  -U_FORTIFY_SOURCE
  -DNO_COMPAT_ISAL_CRYPTO_API_2_24
  -DSAFE_PARAM
  -I"$src_dir/include"
)

asflags=(
  -f elf64
  -D SAFE_DATA
  -I"$src_dir/"
  -I"$src_dir/include/"
  -I"$src_dir/aes/"
  -I"$src_dir/sha256_mb/"
  -I"$src_dir/intel-ipsec-mb/lib/"
)

compile_c() {
  "$CC" "${cflags[@]}" -c "$1" -o "$2"
}

compile_asm() {
  "$AS" "${asflags[@]}" "$1" -o "$2"
}

mkdir -p "$build_dir"
rm -f "$lib_path" "$build_dir"/*.o

# Build only the AES-128 and Zen 2 pinned SHA-256 entry points used by kstuff.
compile_c "$wrapper_src" "$build_dir/isa_l_crypto_ps5_minimal.o"
compile_asm "$src_dir/aes/keyexp_128.asm" "$build_dir/keyexp_128.o"
compile_asm "$src_dir/aes/cbc_dec_128_x8_avx.asm" "$build_dir/cbc_dec_128_x8_avx.o"
compile_asm "$src_dir/aes/XTS_AES_128_enc_expanded_key_avx.asm" \
  "$build_dir/XTS_AES_128_enc_expanded_key_avx.o"
compile_asm "$src_dir/aes/XTS_AES_128_dec_expanded_key_avx.asm" \
  "$build_dir/XTS_AES_128_dec_expanded_key_avx.o"
compile_asm "$repo_root/isa_l_crypto_ps5_sha256_zen2.asm" \
  "$build_dir/isa_l_crypto_ps5_sha256_zen2.o"
compile_asm "$src_dir/sha256_mb/sha256_ni_x1.asm" "$build_dir/sha256_ni_x1.o"

"$AR" crs "$lib_path" \
  "$build_dir/isa_l_crypto_ps5_minimal.o" \
  "$build_dir/keyexp_128.o" \
  "$build_dir/cbc_dec_128_x8_avx.o" \
  "$build_dir/XTS_AES_128_enc_expanded_key_avx.o" \
  "$build_dir/XTS_AES_128_dec_expanded_key_avx.o" \
  "$build_dir/isa_l_crypto_ps5_sha256_zen2.o" \
  "$build_dir/sha256_ni_x1.o"

"$STRIP" -d -R .comment "$lib_path"
