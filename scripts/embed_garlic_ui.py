#!/usr/bin/env python3
"""Embed the Garlic SaveMgr HTML in the source-built PS5 payload."""

from __future__ import annotations

import argparse
from pathlib import Path


def render(symbol: str, data: bytes, mutable: bool = False) -> str:
    storage = "unsigned char" if mutable else "static const unsigned char"
    length_storage = "unsigned int" if mutable else "static const unsigned int"
    lines = [
        "/* Generated from Garlic SaveMgr ui.html. Do not edit. */",
        f"{storage} {symbol}[] = {{",
    ]
    for offset in range(0, len(data), 12):
        chunk = ", ".join(f"0x{value:02x}" for value in data[offset : offset + 12])
        lines.append(f"  {chunk},")
    lines.extend(("};", f"{length_storage} {symbol}_len = {len(data)};", ""))
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--symbol", default="src_ui_html")
    parser.add_argument("--mutable", action="store_true")
    args = parser.parse_args()

    if not args.symbol.isidentifier():
        raise SystemExit("error: --symbol must be a C identifier")
    payload = render(args.symbol, args.input.read_bytes(), args.mutable)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(payload, encoding="utf-8")


if __name__ == "__main__":
    main()
