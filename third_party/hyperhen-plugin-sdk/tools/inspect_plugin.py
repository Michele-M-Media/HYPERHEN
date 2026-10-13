#!/usr/bin/env python3
"""Inspect and validate an HyperHEN plugin ELF."""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

ELF_HEADER_SIZE = 64
ELF_SECTION_SIZE = 64
DESCRIPTOR_SIZE = 128
PLUGIN_ABI_VERSION = 1
KNOWN_CAPABILITIES = (1 << 6) - 1
KNOWN_FLAGS = (1 << 3) - 1
PLUGIN_ID_RE = re.compile(r"^[A-Za-z]{4}[0-9]{5}$")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]{2}$")


def checked_range(offset: int, size: int, total: int, label: str) -> None:
    if offset < 0 or size < 0 or offset > total or size > total - offset:
        raise ValueError(f"{label} is outside the ELF")


def fixed_string(raw: bytes, label: str) -> str:
    terminator = raw.find(b"\0")
    if terminator < 0:
        raise ValueError(f"descriptor {label} is not NUL-terminated")
    try:
        return raw[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"descriptor {label} is not ASCII") from error


def inspect_elf(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < ELF_HEADER_SIZE:
        raise ValueError("ELF header is truncated")
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected a little-endian ELF64")
    if data[6] != 1:
        raise ValueError("unsupported ELF version")

    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    if shentsize < ELF_SECTION_SIZE or shnum == 0 or shstrndx >= shnum:
        raise ValueError("ELF has no valid section table")
    checked_range(shoff, shentsize * shnum, len(data), "section table")

    string_header = shoff + shstrndx * shentsize
    fields = struct.unpack_from("<IIQQQQIIQQ", data, string_header)
    string_offset, string_size = fields[4], fields[5]
    checked_range(string_offset, string_size, len(data), "section string table")
    strings = data[string_offset:string_offset + string_size]

    for index in range(shnum):
        off = shoff + index * shentsize
        fields = struct.unpack_from("<IIQQQQIIQQ", data, off)
        name_offset, _, _, _, section_off, section_size, *_ = fields
        if name_offset >= len(strings):
            raise ValueError("section name is outside the string table")
        end = strings.find(b"\0", name_offset)
        if end < 0:
            raise ValueError("section name is not NUL-terminated")
        name = strings[name_offset:end].decode("ascii", errors="strict")
        if name == ".onion_plugin":
            checked_range(section_off, section_size, len(data), ".onion_plugin")
            if section_size < DESCRIPTOR_SIZE:
                raise ValueError(".onion_plugin descriptor is truncated")
            raw = data[section_off:section_off + section_size]
            size, abi, caps, flags = struct.unpack_from("<IIII", raw, 0)
            if size < DESCRIPTOR_SIZE or size > section_size:
                raise ValueError("descriptor struct_size is invalid")
            if abi != PLUGIN_ABI_VERSION:
                raise ValueError(f"unsupported plugin ABI {abi}")
            if caps & ~KNOWN_CAPABILITIES:
                raise ValueError("descriptor contains unknown capabilities")
            if flags & ~KNOWN_FLAGS:
                raise ValueError("descriptor contains unknown flags")
            plugin_id = fixed_string(raw[16:48], "plugin_id")
            version = fixed_string(raw[48:64], "version")
            plugin_name = fixed_string(raw[64:128], "name")
            if not PLUGIN_ID_RE.fullmatch(plugin_id):
                raise ValueError("plugin_id must contain four letters followed by five digits")
            if not VERSION_RE.fullmatch(version):
                raise ValueError("version must use the x.xx format")
            if not plugin_name:
                raise ValueError("plugin name must not be empty")
            return {"struct_size": size, "abi": abi, "capabilities": caps,
                    "flags": flags, "id": plugin_id, "version": version,
                    "name": plugin_name, "elf_size": len(data)}
    raise ValueError("ELF has no .onion_plugin section")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path,
                        help="standard ELF containing .onion_plugin")
    parser.add_argument("--expect-id")
    parser.add_argument("--expect-version")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    try:
        result = inspect_elf(args.path)
        if args.expect_id is not None and result["id"] != args.expect_id:
            raise ValueError(
                f"descriptor id {result['id']!r} does not match {args.expect_id!r}")
        if (args.expect_version is not None and
                result["version"] != args.expect_version):
            raise ValueError(
                f"descriptor version {result['version']!r} does not match "
                f"{args.expect_version!r}")
    except (OSError, ValueError, UnicodeDecodeError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if not args.quiet:
        print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
