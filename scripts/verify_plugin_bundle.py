#!/usr/bin/env python3
"""Check compressed plugin candidate and isolate changes from HyperDeck r3."""
import argparse
import hashlib
import lzma
from pathlib import Path
import struct
from verify_payload_elf import section_data, elf_extent, MAGIC, InvalidElf

NAMES = ("daemon", "util", "private-loader", "hypercore", "lite-core",
         "automount", "lite-automount", "hypervisor", "hypervisor-5xx",
         "legacy-hen", "hyperdpi", "linux", "klog", "ftp", "debug-loader",
         "hyperdeck")


def payloads_in_section(data):
    cursor = 0
    while (offset := data.find(MAGIC, cursor)) >= 0:
        try:
            size = elf_extent(memoryview(data)[offset:])
        except InvalidElf:
            # ELF validators themselves contain the seven-byte ident literal.
            # A literal is not a valid executable/table layout. Expected payload
            # counts and byte equality below still reject a missing real image.
            cursor = offset + 1
            continue
        marker = (offset + size + 3) & ~3
        assert marker + 4 <= len(data)
        assert struct.unpack_from("<I", data, marker)[0] == size
        payload = data[offset:offset + size]
        elf_extent(payload)
        yield payload
        cursor = marker + 4  # Never mistake nested ELFs for boundaries.


def unpack(path):
    raw = path.read_bytes()
    elf_extent(raw)
    packed = section_data(raw, ".data")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
    boot = decoder.decompress(packed, max_length=128 * 1024 * 1024)
    assert decoder.eof, "incomplete bootstrap"
    size = len(packed) - len(decoder.unused_data)
    at = (size + 3) & ~3
    assert struct.unpack_from("<I", packed, at)[0] == size
    at = (at + 4 + 15) & ~15
    end = packed.index(b"\0", at)
    assert int(packed[at:end]) == len(boot)
    at = (end + 1 + 15) & ~15
    end = packed.index(b"\0", at)
    assert packed[at:end].decode().strip() == hashlib.sha1(boot).hexdigest()
    elf_extent(boot)
    payloads = list(payloads_in_section(section_data(boot, ".data")))
    payloads += list(payloads_in_section(section_data(boot, ".rodata")))
    assert len(payloads) == len(NAMES), f"expected 16 payloads: {len(payloads)}"
    return raw, boot, payloads


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline", type=Path)
    p.add_argument("candidate", type=Path)
    p.add_argument("build", type=Path)
    p.add_argument("--stage-baseline-hyperdeck", type=Path)
    args = p.parse_args()
    _, _, before = unpack(args.baseline)
    if args.stage_baseline_hyperdeck:
        target = args.stage_baseline_hyperdeck
        if target.exists():
            assert target.read_bytes() == before[15], "refusing to replace a different pinned server"
        else:
            target.write_bytes(before[15])
        print("Staged byte-exact baseline HyperDeck server")
        return
    raw, boot, after = unpack(args.candidate)
    changed = [name for name, old, new in zip(NAMES, before, after) if old != new]
    assert changed == ["daemon", "private-loader"], changed
    assert after[0] == (args.build / "bin/daemon.elf").read_bytes()
    assert after[2] == (args.build / "bin/hyperhen_elfldr.elf").read_bytes()
    assert boot == (args.build / "bin/bootstrapper.elf").read_bytes()
    nested = list(payloads_in_section(section_data(after[0], ".text")))
    assert len(nested) == 3, f"unexpected nested daemon payloads: {len(nested)}"
    assert nested[0] == (args.build / "bin/shellui.elf").read_bytes()
    assert nested[1] == after[1], "daemon recovery util is not the pinned util"
    assert nested[2] == after[2], "daemon recovery loader is stale"
    print("PASS: packed bootstrap size/SHA-1; 16 complete payloads; only daemon/private loader changed")
    print("PASS: 14 other payloads byte-identical to r3; daemon embeds new Toolbox/loader and stable util")
    print("SHA256:", hashlib.sha256(raw).hexdigest())


if __name__ == "__main__":
    main()
