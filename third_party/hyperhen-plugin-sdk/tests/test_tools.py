import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def make_elf(plugin_id: str = "TEST00001", version: str = "1.00",
             name: str = "Test plugin", flags: int = 1) -> bytes:
    descriptor = struct.pack("<IIII", 128, 1, 2, flags)
    descriptor += plugin_id.encode() + b"\0" * (32 - len(plugin_id))
    descriptor += version.encode() + b"\0" * (16 - len(version))
    descriptor += name.encode() + b"\0" * (64 - len(name))
    section_names = b"\0.onion_plugin\0.shstrtab\0"
    descriptor_offset = 64
    strings_offset = descriptor_offset + len(descriptor)
    section_table_offset = (strings_offset + len(section_names) + 7) & ~7
    descriptor_section = struct.pack(
        "<IIQQQQIIQQ", 1, 1, 2, 0, descriptor_offset, len(descriptor),
        0, 0, 8, 0)
    string_section = struct.pack(
        "<IIQQQQIIQQ", 15, 3, 0, 0, strings_offset, len(section_names),
        0, 0, 1, 0)
    header = bytearray(64)
    header[:16] = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIQQQIHHHHHH", header, 16, 3, 62, 1, 0, 0,
                     section_table_offset, 0,
                     64, 0, 0, 64, 3, 2)
    image = bytes(header) + descriptor + section_names
    image += b"\0" * (section_table_offset - len(image))
    return image + b"\0" * 64 + descriptor_section + string_section


def test_inspect_and_deploy() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        elf = directory / "sample.elf"
        root = directory / "plugins"
        elf.write_bytes(make_elf())
        inspected = subprocess.run(
            [sys.executable, str(ROOT / "tools/inspect_plugin.py"), str(elf)],
            check=True, capture_output=True, text=True)
        assert json.loads(inspected.stdout)["id"] == "TEST00001"
        subprocess.run(
            [sys.executable, str(ROOT / "tools/deploy_plugin.py"), str(elf),
             "--root", str(root)], check=True)
        installed = root / "TEST00001.elf"
        assert installed.read_bytes() == elf.read_bytes()
        assert not list(root.glob("*.installing"))
