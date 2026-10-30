"""Check the release helper's argument quoting and retained-input staging.

Uses a synthetic SDK and recording CMake command; does not compile or run ELF.
The parent release workflow separately performs real configure/build checks.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile


HELPER = Path(__file__).resolve().parents[1] / "scripts/build_release.sh"


def checksums(directory: Path):
    files = sorted(p for p in directory.rglob("*") if p.is_file())
    (directory / "SHA256SUMS").write_text("".join(
        hashlib.sha256(p.read_bytes()).hexdigest() + "  " +
        p.relative_to(directory).as_posix() + "\n" for p in files))


with tempfile.TemporaryDirectory(prefix="hyperhen helper test ") as temporary:
    base = Path(temporary)
    project = base / "source package with spaces"
    sdk = base / "SDK with spaces"
    binaries = base / "host commands"
    output = base / "build output with spaces"
    for directory in (project / "scripts", project / "source", project / "third_party",
                      project / "release-components", project / "release-inputs",
                      sdk / "bin", sdk / "toolchain", sdk / "target/lib",
                      sdk / "target/include/c++/v1", binaries):
        directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(HELPER, project / "scripts/build_release.sh")
    (project / "VERSION").write_text("0.9.4\n")
    (project / "source/CMakeLists.txt").write_text("# recording fixture\n")
    (sdk / "toolchain/prospero.cmake").write_text("# recording fixture\n")
    for tool in ("prospero-clang", "prospero-clang++", "prospero-as", "prospero-lld"):
        path = sdk / "bin" / tool
        path.write_text("#!/bin/sh\nexit 0\n")
        path.chmod(0o755)
    for library in ("libc++.a", "libc++abi.a", "libunwind.a"):
        (sdk / "target/lib" / library).write_bytes(b"test-only runtime")
    for name in ("hyperhen_elfldr_inner", "hyperhen_elfldr", "hyperdeck-pkg-helper",
                 "liveshell", "hypercore_940", "hypercore_1270", "hypervisor_1270",
                 "hypervisor_940", "hyperlinux_1360", "ps5-fw-spoof",
                 "np-fake-signin-ps5", "nanodns", "chukei", "zftpd",
                 "hyper_assistant_r45", "hypercore_13xx_v092"):
        (project / "release-components" / (name + ".elf")).write_bytes(name.encode())
    for name in ("game-compressor", "web-file-manager", "cheatrunner", "ps5upload"):
        path = project / "release-inputs/hyperdeck-engines" / (name + ".elf")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(name.encode())
    for name in ("lib/libmicrohttpd.a", "include/microhttpd.h"):
        path = project / "release-inputs/websrv-http" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(name.encode())
    for name in ("sdk/crt1-r42-1360.o", "hyper/hyper-voices.zip"):
        path = project / "release-inputs" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"test-only retained input: " + name.encode())
    checksums(project / "release-components")
    checksums(project / "release-inputs")
    cmake = binaries / "cmake"
    cmake.write_text("#!/usr/bin/env python3\nimport json,os,sys\n"
                     "open(os.environ['CMAKE_RECORD'],'w').write(json.dumps(sys.argv[1:]))\n")
    cmake.chmod(0o755)
    record = base / "cmake argv.json"
    environment = os.environ | {"PS5_PAYLOAD_SDK": str(sdk), "CMAKE_RECORD": str(record),
                                "PATH": str(binaries) + ":" + os.environ["PATH"]}
    command = ["bash", str(project / "scripts/build_release.sh"), "--configure-only",
               "--build-dir", str(output), "--jobs", "3"]
    subprocess.run(command, env=environment, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, text=True)
    arguments = json.loads(record.read_text())
    assert arguments[:6] == ["-S", str(project / "source"), "-B", str(output), "-G", "Ninja"]
    assert "-DCMAKE_TOOLCHAIN_FILE=" + str(sdk / "toolchain/prospero.cmake") in arguments
    assert "-DHYPERHEN_SOURCE_PLUGIN_RUNTIME=ON" in arguments
    assert "-DHYPERHEN_STABLE_HYPERDECK_ELF=" in arguments
    assert "-DHYPERHEN_STABLE_INNER_LOADER_ELF=" + str(project / "release-components/hyperhen_elfldr_inner.elf") in arguments
    assert "-DHYPERHEN_STABLE_PKG_HELPER_ELF=" + str(project / "release-components/hyperdeck-pkg-helper.elf") in arguments
    crt = str(project / "release-inputs/sdk/crt1-r42-1360.o")
    assert "-DCMAKE_C_STANDARD_LIBRARIES=" + crt in arguments
    assert "-DCMAKE_CXX_STANDARD_LIBRARIES=" + crt in arguments
    assert any(a.startswith("-DCMAKE_EXE_LINKER_FLAGS=") and "-nostartfiles" in a for a in arguments)
    assert "-DHYPERHEN_INTEGRATED_HYPER=ON" in arguments
    assert "-DHYPER_ASSISTANT_ELF=" + str(project / "release-components/hyper_assistant_r45.elf") in arguments
    assert "-DHYPERHEN_CORE_13XX_V092_ELF=" + str(project / "release-components/hypercore_13xx_v092.elf") in arguments
    assert "-DHYPER_VOICES_ZIP=" + str(project / "release-inputs/hyper/hyper-voices.zip") in arguments
    for source in (project / "release-inputs").rglob("*"):
        if source.is_file() and source.relative_to(project / "release-inputs").parts[0] in ("hyperdeck-engines", "websrv-http"):
            expected = output / "vendor" / source.relative_to(project / "release-inputs")
            assert expected.read_bytes() == source.read_bytes()
    debug_mappings = {"ps5-fw-spoof": "ps5-fw-spoof",
                      "np-fake-signin-ps5": "np-fake-signin-ps5", "nanodns": "nanodns",
                      "chukei": "chukei-ps5", "zftpd": "zftpd-ps5-zhttp-v1.6.0"}
    for retained, destination in debug_mappings.items():
        assert (project / "third_party/debug-tools" / (destination + ".elf")).read_bytes() == \
               (project / "release-components" / (retained + ".elf")).read_bytes()
    # A locally changed destination must never be silently replaced.
    destination = project / "third_party/debug-tools/nanodns.elf"
    destination.write_bytes(b"locally modified")
    unexpected = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
    assert unexpected.returncode != 0 and "differs from the release" in unexpected.stderr
    assert destination.read_bytes() == b"locally modified"
    destination.write_bytes((project / "release-components/nanodns.elf").read_bytes())
    invalid = subprocess.run(command[:-2] + ["--jobs", "0"], env=environment,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert invalid.returncode != 0 and "positive integer" in invalid.stderr
    # Input corruption must stop before a new configure can be recorded.
    record.unlink()
    (project / "release-inputs/hyperdeck-engines/game-compressor.elf").write_bytes(b"corrupt")
    rejected = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)
    assert rejected.returncode != 0 and not record.exists()
print("Release helper: spaced project/SDK/build paths, correct pins, thin-checkout inputs, local mismatch protection, invalid jobs and corrupt input rejection PASS")
