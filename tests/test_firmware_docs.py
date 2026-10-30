"""Compare the published firmware table with the actual source router."""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--readme", type=Path)
parser.add_argument("--policy-header", type=Path)
arguments = parser.parse_args()
project = arguments.project_root.resolve()
readme = arguments.readme or project / "README.md"
router = project / "source/include/onion/firmware_capabilities.h"
selector = project / "source/include/onion/engine_selector.h"
policy = arguments.policy_header or project / "third_party/shadowmountplus/include/sm_hyperhen_policy.h"


def source_body(path, name):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", path.read_text(), flags=re.S)
    body = re.search(r"\b" + re.escape(name) + r"\([^)]*\)\s*\{(.*?)\n\}", text, re.S)
    assert body, f"Missing function: {name}"
    return body[1]


def firmware(value):
    assert value & 0xffff == 0, "Unexpected firmware suffix"
    return f"{(value >> 24) & 255:X}.{(value >> 16) & 255:02X}"


routes = {}
pending = []
body = source_body(router, "onion_firmware_route")
for match in re.finditer(r"case\s+0x([0-9a-fA-F]{8})[uU]?\s*:|return\s+(ONION_FIRMWARE_ROUTE_[A-Z0-9_]+)\s*;", body):
    if match[1]:
        pending.append(firmware(int(match[1], 16)))
    else:
        for version in pending:
            assert version not in routes, f"Duplicate source profile: {version}"
            routes[version] = match[2]
        pending.clear()
assert not pending
assert len(routes) == 67

labels = {
    "Legacy selector": "ONION_FIRMWARE_ROUTE_LEGACY_HEN",
    "5.xx selector": "ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX",
    "A53/PPR": "ONION_FIRMWARE_ROUTE_A53_PPR",
    "Dedicated 9.40": "ONION_FIRMWARE_ROUTE_HYPERVISOR_940",
    "HyperCore without PPR": "ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR",
    "Hypervisor route": "ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX",
    "Kstuff-lite": "ONION_FIRMWARE_ROUTE_KSTUFF_LITE",
}
text = readme.read_text(encoding="utf-8")
table = text.split("<!-- PROFILE_TABLE_BEGIN -->", 1)[1].split("<!-- PROFILE_TABLE_END -->", 1)[0]
documented = {}
marked_limited = set()
counts = []
for line in table.splitlines():
    cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
    if not cells or cells[0] not in labels:
        continue
    versions = re.findall(r"(?<![\d.])\d+\.\d{2}(?![\d.])", cells[2])
    assert int(cells[1]) == len(versions), f"Count mismatch: {cells[0]}"
    assert len(versions) == len(set(versions)), f"Duplicate row: {cells[0]}"
    counts.append(int(cells[1]))
    marked_limited.update(re.findall(r"(\d+\.\d{2})†", cells[2]))
    for version in versions:
        assert version not in documented, f"Duplicate documented profile: {version}"
        documented[version] = labels[cells[0]]
assert sum(counts) == 67 and sorted(counts) == [1, 2, 4, 4, 6, 25, 25]
assert documented == routes, f"Firmware table differs from source: {set(documented.items()) ^ set(routes.items())}"

enabled = {firmware(int(value, 16)) for value in re.findall(
    r"case\s+0x([0-9a-fA-F]{8})[uU]?\s*:",
    source_body(selector, "onion_engine_selector_hypercore_available"))}
legacy_enabled = {version for version, route in routes.items()
                  if route == labels["Legacy selector"] and int(version.split('.')[0]) >= 3}
five_enabled = {version for version, route in routes.items() if route == labels["5.xx selector"]}
assert enabled == legacy_enabled | five_enabled
assert not any(int(version.split('.')[0]) < 3 for version in enabled)

limited = {firmware(int(value, 16)) for value in re.findall(
    r"case\s+0x([0-9a-fA-F]{8})[uU]?\s*:",
    source_body(policy, "sm_hyperhen_shellcore_limited_firmware"))}
assert marked_limited == limited == {"1.05", "1.10", "1.11", "1.13", "4.02"}
public_docs = [readme.parent / name for name in
               ("README.md", "README_ZH.md", "RELEASE_NOTES.md", "BUILD_RELEASE.md")]
public_docs.extend((readme.parent / "docs").rglob("*.md"))
for path in public_docs:
    if path.exists():
        assert not re.search(r"HyperShell|HyperAI", path.read_text(encoding="utf-8")), f"Unwanted project description in {path}"
print("Firmware documentation: all 67 exact source routes, selector availability, five limited profiles and public-description naming PASS")
