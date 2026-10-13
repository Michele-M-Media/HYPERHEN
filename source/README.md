# HyperHEN — source tree

This directory contains the **HyperHEN** CMake targets and first-party source,
licensed under **GPLv3**. See the [root README](../README.md) for supported
features, usage, configuration, and credits.

## Layout

| Path | Description |
|------|-------------|
| `bootstrapper/` | Main payload bootstrapper |
| `daemon/` | Main daemon |
| `util/` | Utility daemon (cheats, IPC, …) |
| `shellui/` | Toolbox / ShellUI hooks |
| `i18n/` | Shared Toolbox / notification locale catalogs (see [its README](i18n/README.md)) |
| `unpacker/` | Payload unpacker |
| `libhijacker/`, `libNineS/`, `libNidResolver/`, `libonion_sprx/` | Internal static libs |
| `include/` | Shared headers |
| `common/` | Shared low-level implementations |
| `platform/ps5/stubs/` | PS5 system-library link stubs |

External source dependencies live in the repository-level
[`third_party/`](../third_party/) directory. HyperCore is built from the
source-pinned `kstuff-a53` snapshot; no prebuilt kstuff ELF is downloaded.
Repository-side generators and build helpers live in [`../tools/`](../tools/).

## Build

Requires a Prospero / PS5 payload SDK (`PS5_PAYLOAD_SDK`) and clang targeting `x86_64-sie-ps5`.

**Full pipeline** (preferred) from the repo root:

```bash
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh
```

Builds HyperCore, then shellui → daemon/util → bootstrapper → unpacker.
See [`../third_party/README.md`](../third_party/README.md) and
`./scripts/build.sh --help`.

Manual CMake:

```bash
./scripts/ps5_cmake.sh -S source -B build -G Ninja
cmake --build build
```

Build products go under `build/bin/` and `build/lib/` (repo root; gitignored).

## Further reading

- Project overview & credits: [`../README.md`](../README.md)
- Writeups: [`../docs/`](../docs/)
- Upstream lineage: [etaHEN](https://github.com/LightningMods/etaHEN) · [GoldHEN](https://github.com/GoldHEN/GoldHEN)
