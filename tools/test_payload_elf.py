#!/usr/bin/env python3
"""Regression checks for truncated and nested payload extraction."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_payload_elf import (DEBUG_SUITE_RELEASE_COMPONENTS,
                                DEDICATED_1270_RELEASE_COMPONENTS, EHDR,
                                LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS, MAGIC,
                                PHDR, RELEASE_LAYOUTS, SHDR,
                                InvalidElf, elf_extent, embedded_payloads)


def image(length=1024, nested=None, osabi=0):
    data = bytearray(length)
    ident = b"\x7fELF\x02\x01\x01" + bytes([osabi]) + bytes(8)
    data[:EHDR.size] = EHDR.pack(
        ident, 3, 62, 1, 0x1080, 64, length - 128, 0, 64, 56, 1, 64, 2, 1)
    data[64:120] = PHDR.pack(1, 5, 0, 0x1000, 0, length - 128, length, 0x4000)
    # A small name table, followed by its section header at EOF.
    data[128:139] = b"\0.shstrtab\0"
    data[-64:] = SHDR.pack(1, 3, 0, 0, 128, 11, 0, 0, 1, 0)
    if nested:
        data[256:256 + len(nested)] = nested
    return bytes(data)


class PayloadIntegrityTests(unittest.TestCase):
    def test_complete_payload(self):
        data = image()
        self.assertEqual(len(data), elf_extent(data))

    def test_truncated_section_table(self):
        with self.assertRaisesRegex(InvalidElf, "truncated section table"):
            elf_extent(image()[:-10])

    def test_truncated_file_backed_segment(self):
        data = bytearray(image())
        struct.pack_into("<Q", data, 64 + 32, len(data) + 100)
        struct.pack_into("<Q", data, 64 + 40, len(data) + 100)
        with self.assertRaisesRegex(InvalidElf, "truncated segment"):
            elf_extent(data)

    def test_bss_need_not_exist_in_file(self):
        data = bytearray(image())
        data[-64:] = SHDR.pack(0, 8, 0, 0, 8192, 16384, 0, 0, 1, 0)
        self.assertEqual(len(data), elf_extent(data))

    def test_nested_magic_is_not_an_end_boundary(self):
        child = image()
        parent = image(2048, child)
        following = image()
        container = parent + struct.pack("<I", len(parent))
        container += bytes(12) + following + struct.pack("<I", len(following))
        self.assertEqual([parent, following], list(embedded_payloads(container)))

    def test_incidental_short_magic_is_ignored(self):
        false_match = MAGIC + b"\0Symbol relocation reference"
        payload = image()
        container = false_match + bytes(16)
        payload_end = len(container) + len(payload)
        container += payload + bytes((-payload_end) % 4)
        container += struct.pack("<I", len(payload))
        self.assertEqual([payload], list(embedded_payloads(container)))

    def test_freebsd_osabi_payload_is_discovered(self):
        payload = image(osabi=9)
        container = payload + struct.pack("<I", len(payload))
        self.assertEqual([payload], list(embedded_payloads(container)))

    def test_wrong_recorded_size_rejected(self):
        parent = image(2048, image())
        with self.assertRaisesRegex(InvalidElf, "size field"):
            list(embedded_payloads(parent + struct.pack("<I", 1024)))

    def test_1270_release_layout_is_unambiguous(self):
        self.assertEqual(
            DEDICATED_1270_RELEASE_COMPONENTS,
            RELEASE_LAYOUTS[len(DEDICATED_1270_RELEASE_COMPONENTS)])
        self.assertEqual(21, len(DEDICATED_1270_RELEASE_COMPONENTS))
        self.assertEqual(len(DEDICATED_1270_RELEASE_COMPONENTS),
                         len(set(DEDICATED_1270_RELEASE_COMPONENTS)))
        self.assertEqual("hypercore_940",
                         DEDICATED_1270_RELEASE_COMPONENTS[5])
        self.assertEqual("hypercore_1270",
                         DEDICATED_1270_RELEASE_COMPONENTS[6])
        self.assertEqual("hypervisor_1270",
                         DEDICATED_1270_RELEASE_COMPONENTS[10])

    def test_debug_suite_release_layout_is_unambiguous(self):
        self.assertEqual(
            DEBUG_SUITE_RELEASE_COMPONENTS,
            RELEASE_LAYOUTS[len(DEBUG_SUITE_RELEASE_COMPONENTS)])
        self.assertEqual(26, len(DEBUG_SUITE_RELEASE_COMPONENTS))
        self.assertEqual(len(DEBUG_SUITE_RELEASE_COMPONENTS),
                         len(set(DEBUG_SUITE_RELEASE_COMPONENTS)))
        self.assertEqual(DEDICATED_1270_RELEASE_COMPONENTS,
                         DEBUG_SUITE_RELEASE_COMPONENTS[:21])
        self.assertEqual(
            ("fw_spoof", "np_fake_signin", "nanodns", "chukei", "zftpd"),
            DEBUG_SUITE_RELEASE_COMPONENTS[21:])

    def test_legacy_debug_suite_release_layout_is_still_readable(self):
        self.assertEqual(28, len(LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS))
        self.assertEqual(
            LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS,
            RELEASE_LAYOUTS[len(LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS)])
        self.assertEqual(
            ("fw_spoof", "np_fake_signin", "liveshell", "nanodns",
             "ps5upload", "chukei", "zftpd"),
            LEGACY_DEBUG_SUITE_RELEASE_COMPONENTS[21:])


if __name__ == "__main__":
    unittest.main()
