#!/usr/bin/env python3
"""Generate C string literal for XOR-encrypted version prefix.

Uses the same key as runtime: base64_decode("U0lTVFIwX0lfU0VFX1lPVQ==")
→ b"SISTR0_I_SEE_YOU" (not the Base64 text itself).
"""

from __future__ import annotations

import argparse
import base64

KEY_B64 = b"U0lTVFIwX0lfU0VFX1lPVQ=="


def xor_bytes(data: bytes, key: bytes) -> bytes:
    kl = len(key)
    return bytes(b ^ key[i % kl] for i, b in enumerate(data))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="XOR-encrypt a string for shellui enc_ver (decoded b64 key)."
    )
    parser.add_argument("input_string", type=str, help="Plain version prefix")
    args = parser.parse_args()

    key = base64.b64decode(KEY_B64)
    data = args.input_string.encode()
    encrypted = xor_bytes(data, key)
    cstr = "".join(f"\\x{b:02x}" for b in encrypted)
    print(f'const char enc_ver[] = "{cstr}"; /* {args.input_string!r} */')
    assert xor_bytes(encrypted, key) == data


if __name__ == "__main__":
    main()
