#!/usr/bin/env python3
"""Portable `xxd -i` replacement for embedding shellcode blobs.

Emits the same symbol naming xxd uses: a path such as
`shellcode_hv.bin` becomes `unsigned char shellcode_hv_bin[]` plus
`unsigned int shellcode_hv_bin_len`.
"""
import os
import sys


def symbol_name(path: str) -> str:
    base = os.path.basename(path)
    return "".join(c if c.isalnum() else "_" for c in base)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <file>", file=sys.stderr)
        return 2

    path = sys.argv[1]
    with open(path, "rb") as fh:
        data = fh.read()

    name = symbol_name(path)
    out = sys.stdout
    out.write(f"unsigned char {name}[] = {{\n")
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        out.write("  " + ", ".join(f"0x{b:02x}" for b in chunk))
        out.write(",\n" if i + 12 < len(data) else "\n")
    out.write("};\n")
    out.write(f"unsigned int {name}_len = {len(data)};\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
