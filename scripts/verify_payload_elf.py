#!/usr/bin/env python3
"""Validate payload boundaries before embedding, or extract a verified release.

ELFs can themselves contain ELFs. Searching for the next ELF magic is NOT a
valid way to find the end of a payload; use its tables and recorded size.
This reader never rewrites its inputs (unlike objcopy without an output file).
"""

import argparse
import hashlib
import json
import lzma
from pathlib import Path
import struct
import sys

EHDR = struct.Struct("<16sHHIQQQIHHHHHH")
PHDR = struct.Struct("<IIQQQQQQ")
SHDR = struct.Struct("<IIQQQQIIQQ")
MAGIC = b"\x7fELF\x02\x01\x01"
# Embedded PS5 payloads use the System V ABI and zeroed e_ident padding.
# Searching only the seven-byte class/data/version prefix can mistake text or
# media assets for an ELF before reaching the real .incbin payload.
# PS5 payloads in this tree use either System V (0) or FreeBSD (9) OSABI.
# Keep the complete 16-byte identities so media/text containing a short ELF
# prefix cannot be mistaken for an embedded component boundary.
EMBEDDED_IDENTS = (MAGIC + bytes(9), MAGIC + b"\x09" + bytes(8))
LEGACY_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "automount",
    "hypervisor", "hypervisor_5xx", "legacy_hen", "hyperdpi", "hyperlinux",
    "klogsrv", "ftpsrv", "debug_elfldr",
)
COMPAT_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "lite_core",
    "automount", "lite_automount", "hypervisor", "hypervisor_5xx",
    "legacy_hen", "hyperdpi", "hyperlinux", "klogsrv", "ftpsrv",
    "debug_elfldr",
)
EXTENDED_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "lite_core",
    "automount", "lite_automount", "hypervisor", "hypervisor_5xx",
    "legacy_hen", "hyperdpi", "hyperlinux", "klogsrv", "ftpsrv",
    "garlic", "debug_elfldr",
)
CURRENT_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "lite_core",
    "automount", "lite_automount", "hypervisor", "hypervisor_5xx",
    "legacy_hen", "hyperdpi", "hyperlinux", "hyperlinux_1360", "klogsrv",
    "ftpsrv", "garlic", "debug_elfldr",
)
HYPERVISOR_940_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "lite_core",
    "automount", "lite_automount", "hypervisor", "hypervisor_5xx",
    "hypervisor_940", "legacy_hen", "hyperdpi", "hyperlinux",
    "hyperlinux_1360", "klogsrv", "ftpsrv", "garlic", "debug_elfldr",
)
DEDICATED_940_CORE_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "lite_core",
    "hypercore_940", "automount", "lite_automount", "hypervisor",
    "hypervisor_5xx", "hypervisor_940", "legacy_hen", "hyperdpi",
    "hyperlinux", "hyperlinux_1360", "klogsrv", "ftpsrv", "garlic",
    "debug_elfldr",
)
DEDICATED_1270_RELEASE_COMPONENTS = (
    "daemon", "util", "hyperhen_elfldr", "hypercore", "lite_core",
    "hypercore_940", "hypercore_1270", "automount", "lite_automount",
    "hypervisor", "hypervisor_1270", "hypervisor_5xx", "hypervisor_940",
    "legacy_hen", "hyperdpi", "hyperlinux", "hyperlinux_1360", "klogsrv",
    "ftpsrv", "garlic", "debug_elfldr",
)
LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS = (
    *DEDICATED_1270_RELEASE_COMPONENTS,
    "fw_spoof", "np_fake_signin", "liveshell", "nanodns", "ps5upload",
    "chukei", "zftpd",
)
DEBUG_SUITE_RELEASE_COMPONENTS = (
    *DEDICATED_1270_RELEASE_COMPONENTS,
    "fw_spoof", "np_fake_signin", "nanodns", "chukei", "zftpd",
)
RELEASE_LAYOUTS = {
    len(LEGACY_RELEASE_COMPONENTS): LEGACY_RELEASE_COMPONENTS,
    len(COMPAT_RELEASE_COMPONENTS): COMPAT_RELEASE_COMPONENTS,
    len(EXTENDED_RELEASE_COMPONENTS): EXTENDED_RELEASE_COMPONENTS,
    len(CURRENT_RELEASE_COMPONENTS): CURRENT_RELEASE_COMPONENTS,
    len(HYPERVISOR_940_RELEASE_COMPONENTS): HYPERVISOR_940_RELEASE_COMPONENTS,
    len(DEDICATED_940_CORE_RELEASE_COMPONENTS):
        DEDICATED_940_CORE_RELEASE_COMPONENTS,
    len(DEDICATED_1270_RELEASE_COMPONENTS):
        DEDICATED_1270_RELEASE_COMPONENTS,
    len(LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS):
        LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS,
    len(DEBUG_SUITE_RELEASE_COMPONENTS): DEBUG_SUITE_RELEASE_COMPONENTS,
}


class InvalidElf(ValueError):
    pass


def elf_extent(data):
    """Return the full file extent, rejecting any table/segment past EOF."""
    def bounded(offset, size, what):
        end = offset + size
        if end > len(data):
            raise InvalidElf(
                f"truncated {what}: requires {end} bytes, has {len(data)}")
        return end

    bounded(0, EHDR.size, "ELF header")
    h = EHDR.unpack_from(data)
    if not data[:7] == MAGIC or h[1] not in (2, 3) or h[2:4] != (62, 1):
        raise InvalidElf("expected a little-endian x86-64 ELF executable")
    if h[8] != EHDR.size or h[9] != PHDR.size or h[11] != SHDR.size:
        raise InvalidElf("invalid ELF header/table entry sizes")
    if not h[10] or not h[12] or not 0 < h[13] < h[12]:
        raise InvalidElf("missing ELF program/section/name table")
    extent = max(
        bounded(h[5], h[9] * h[10], "program table"),
        bounded(h[6], h[11] * h[12], "section table"),
    )
    executable_entry = False
    for i in range(h[10]):
        kind, flags, offset, addr, _, size, memsize, _ = PHDR.unpack_from(
            data, h[5] + i * h[9])
        extent = max(extent, bounded(offset, size, f"segment {i}"))
        if kind == 1:
            if size > memsize:
                raise InvalidElf(f"segment {i}: file size exceeds memory size")
            executable_entry |= bool(flags & 1 and addr <= h[4] < addr + size)
    if not executable_entry:
        raise InvalidElf("entry point is outside executable file-backed segments")
    for i in range(h[12]):
        section = SHDR.unpack_from(data, h[6] + i * h[11])
        if section[1] != 8:  # SHT_NOBITS occupies memory, not file bytes.
            extent = max(extent, bounded(section[4], section[5], f"section {i}"))
    return extent


def section_data(data, name):
    elf_extent(data)
    h = EHDR.unpack_from(data)
    headers = [SHDR.unpack_from(data, h[6] + i * h[11]) for i in range(h[12])]
    strings = headers[h[13]]
    names = data[strings[4]:strings[4] + strings[5]]
    for s in headers:
        if names[s[0]:].split(b"\0", 1)[0] == name.encode():
            return data[s[4]:s[4] + s[5]]
    raise InvalidElf(f"missing {name} section")


def embedded_payloads(data):
    """Read .incbin payloads followed by their aligned uint32 size field."""
    cursor = 0
    while True:
        candidates = [data.find(ident, cursor) for ident in EMBEDDED_IDENTS]
        candidates = [offset for offset in candidates if offset >= 0]
        if not candidates:
            break
        offset = min(candidates)
        size = elf_extent(memoryview(data)[offset:])
        marker = (offset + size + 3) & ~3
        if marker + 4 > len(data):
            raise InvalidElf("missing embedded payload size field")
        recorded = struct.unpack_from("<I", data, marker)[0]
        if recorded != size:
            raise InvalidElf(f"payload at {offset}: extent {size}, size field {recorded}")
        payload = data[offset:offset + size]
        elf_extent(payload)  # Validate the slice, not the containing buffer.
        yield payload
        cursor = marker + 4  # Skip every nested ELF inside this payload.


def extract_release(path, expected_sha256, destination):
    raw = path.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != expected_sha256:
        raise InvalidElf(f"release SHA-256 mismatch: {actual}")
    packed = section_data(raw, ".data")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
    bootstrap = decoder.decompress(packed, max_length=128 * 1024 * 1024)
    if not decoder.eof:
        raise InvalidElf("incomplete or oversized compressed bootstrap")
    compressed_size = len(packed) - len(decoder.unused_data)
    size_field = (compressed_size + 3) & ~3
    if struct.unpack_from("<I", packed, size_field)[0] != compressed_size:
        raise InvalidElf("compressed bootstrap size field mismatch")
    size_text = (size_field + 4 + 15) & ~15
    size_end = packed.index(b"\0", size_text)
    digest_text = (size_end + 1 + 15) & ~15
    digest_end = packed.index(b"\0", digest_text)
    expected_size = int(packed[size_text:size_end])
    expected_sha1 = packed[digest_text:digest_end].decode().strip()
    if len(bootstrap) != expected_size or hashlib.sha1(bootstrap).hexdigest() != expected_sha1:
        raise InvalidElf("decompressed bootstrap size or SHA-1 mismatch")
    components = list(embedded_payloads(section_data(bootstrap, ".data")))
    component_names = RELEASE_LAYOUTS.get(len(components))
    if component_names is None:
        expected = ", ".join(str(count) for count in sorted(RELEASE_LAYOUTS))
        raise InvalidElf(
            f"expected a known release component count ({expected}), "
            f"found {len(components)}")
    if destination.exists() and any(destination.iterdir()):
        raise InvalidElf("use an empty output directory to preserve prior inputs")
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {"release_sha256": actual, "bootstrap_sha1": expected_sha1, "components": {}}
    for name, payload in zip(component_names, components):
        (destination / f"{name}.elf").write_bytes(payload)
        digest = hashlib.sha256(payload).hexdigest()
        manifest["components"][name] = {"size": len(payload), "sha256": digest}
        print(f"{name}.elf: {len(payload)} bytes {digest}")
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check")
    check.add_argument("--embedded", action="store_true",
                       help="also check .incbin ELFs in .data and .rodata")
    check.add_argument("files", nargs="+", type=Path)
    extract = commands.add_parser("extract-release")
    extract.add_argument("release", type=Path)
    extract.add_argument("--sha256", required=True)
    extract.add_argument("--output", required=True, type=Path)
    distinct = commands.add_parser("distinct")
    distinct.add_argument("files", nargs=2, type=Path)
    args = parser.parse_args()
    try:
        if args.command == "extract-release":
            extract_release(args.release, args.sha256, args.output)
        elif args.command == "distinct":
            left, right = (path.read_bytes() for path in args.files)
            if hashlib.sha256(left).digest() == hashlib.sha256(right).digest():
                raise InvalidElf("payloads required to be distinct are byte-identical")
            print("Payload identities are distinct")
        else:
            for path in args.files:
                data = path.read_bytes()
                elf_extent(data)
                print(f"ELF complete: {path.name} ({len(data)} bytes)")
                if args.embedded:
                    for name in (".data", ".rodata"):
                        payloads = list(embedded_payloads(section_data(data, name)))
                        print(f"  {name}: {len(payloads)} complete embedded payloads")
    except (OSError, ValueError, struct.error, lzma.LZMAError) as error:
        parser.exit(1, f"Payload validation failed: {error}\n")


if __name__ == "__main__":
    main()
