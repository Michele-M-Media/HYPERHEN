# HyperHEN components and technical overview

This document is the public technical inventory for HyperHEN. It describes
what is built into the release, what remains an external dependency, how the
firmware routes differ, and where each major component came from.

HyperHEN is an integration project. It does not contain the initial PS5 kernel
exploit and does not make an unmodified console load payloads. The first launch
requires an already jailbroken system and an external `elfldr` endpoint.

## Project ownership

HyperHEN integration, branding, firmware-route orchestration, release
engineering, and project maintenance are by **MICHELE MEDIA**.

The project is based on public PS5 homebrew and reverse-engineering work. All
vendored upstream notices and licenses remain authoritative for their files.

## Release architecture

The distributed `HyperHEN.elf` is a wrapper around a compressed bootstrapper.
The wrapper verifies the expected size and SHA-1 of the unpacked bootstrapper
before execution. The bootstrapper then selects one exact firmware route and
starts the remaining embedded services in a fixed order.

```text
external exploit host + elfldr:9021
                 |
                 v
           HyperHEN.elf
                 |
                 v
      verified LZMA bootstrapper
                 |
                 +--> one-shot HyperLinux handoff, when armed
                 |
                 +--> exact firmware engine
                 |      |- legacy HEN: 1.xx-4.51
                 |      |- 5.xx Byepervisor -> HyperCore
                 |      |- 13.xx Byepervisor -> Rest Mode -> HyperCore
                 |      `- A53/PPR -> HyperCore on the remaining profiled route
                 |
                 +--> HyperHEN Automount
                 +--> daemon + utility + ShellUI Toolbox
                 `--> HyperDPI, when enabled and supported
```

After the first hop, HyperHEN attempts to start its private loader on TCP port
`9020`. Port `9021` remains the fallback supplied by the exploit environment.
Embedded stages are launched serially and synchronize through explicit ready
and failure markers; a dependent stage does not start after a failed required
stage.

## Firmware routing

Firmware matching is exact. A range in a project description must not be read
as automatic support for every version inside that range.

| Route | Exact firmware profiles | Startup contract | Components after the route |
| --- | --- | --- | --- |
| Legacy HEN | 1.00, 1.01, 1.02, 1.05, 1.10, 1.11, 1.12, 1.13, 1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00, 3.10, 3.20, 3.21, 4.00, 4.02, 4.03, 4.50, 4.51 | The embedded `ps5-hen` engine must publish its completion marker. HyperCore and the newer daemon route are not started on this branch. | HyperHEN Automount; legacy-ready notification |
| Byepervisor 5.xx | 5.00, 5.02, 5.10, 5.50 | The embedded 5.xx stage must complete and publish its readiness marker before HyperCore starts. | HyperCore, Automount, daemon, utility, Toolbox, HyperDPI |
| HyperCore A53/PPR | Profiles with complete source tables outside the legacy and dedicated hypervisor routes | The A53/PPR loader prepares the required environment and starts the source-built kstuff core. Missing required profile data fails closed. | Automount, daemon, utility, Toolbox, HyperDPI |
| Byepervisor Reborn | 13.00, 13.20, 13.60 | The stage arms the Rest Mode path, waits for resume, and must publish readiness before HyperCore continues. | HyperCore, Automount, daemon, utility, Toolbox, HyperDPI |

The source-built HyperCore contains ShellCore tables for 1.00, 1.01, 1.02,
1.12, 1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00, 3.10, 3.20,
3.21, 4.00, 4.02, 4.03, 4.50, 4.51, 5.00, 5.02, 5.10, 5.50, 6.00, 6.02,
6.50, 7.00, 7.01, 7.20, 7.40, 7.60, 7.61, 8.00, 8.20, 8.40, 8.60, 9.00,
9.05, 9.20, 9.40, 9.60, 10.00, 10.01, 10.20, 10.40, 10.60, 11.00, 11.20,
11.40, 11.60, 12.00, 12.02, 12.20, 12.40, 12.60, 12.70, 13.00, 13.20,
13.40, 13.42, and 13.60. These tables are compatibility data, not a claim
that the complete HyperHEN stack was tested on every listed firmware.

The combined HyperCore path has console-log validation on firmware 5.00.
Additional profiles require hardware testing. Unsupported, incomplete, or
non-exact firmware profiles are rejected instead of borrowing offsets from a
nearby release.

## Core components

| Component | Source or revision | Inclusion | HyperHEN role |
| --- | --- | --- | --- |
| [OnionHEN](https://github.com/aydencharles/onionHEN) | Supplied source base | Adapted in tree | Modular HEN/Toolbox structure and retained compatibility interfaces |
| [etaHEN](https://github.com/LightningMods/etaHEN) | Upstream ancestry of the supplied tree | Adapted in tree | Original daemon, utility, ShellUI, payload, overlay, and cheat architecture |
| [GoldHEN](https://github.com/GoldHEN/GoldHEN) | Design reference | Not embedded | All-in-one HEN product model and user-experience inspiration |
| [kstuff-a53](https://github.com/Soonniicc/kstuff-a53) | `0a69ea31759f048d24b38bfaa23dbec1fd065916` | Built from vendored source | HyperCore integration container |
| [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) | Base `2506a5b15af501734f7ea3dce2759c077cdb76c6` | Built as part of HyperCore | Kernel support and runtime core |
| [ppr-patch](https://github.com/drakmor/ppr-patch) | Base `fd4c8224563130e9698d3b2b2f44712826ceb525` | Adapted into the HyperCore loader | A53/PPR preparation and transport |
| [BackPork](https://github.com/BestPig/BackPork) | Supplied integration base | Built as part of HyperCore | Process monitoring and the original `app0`/`fakelib` mount path; paused when HyperHEN Automount owns the lifecycle |
| [a53-code-exec](https://github.com/cragson/a53-code-exec) | Technical reference | Reference only; its ELF is not shipped | A53 research reference |
| [Byepervisor Reborn](https://github.com/PS5Dev/Byepervisor-Reborn) | `ea04f7132b837087f552c57261cb800481a3a910` | Built from vendored source | Rest-Mode hypervisor stage for 13.00, 13.20, and 13.60 |
| Byepervisor 5.xx | `9c182a1622dd43ea56641a7080638af3673a723f` | Built from supplied source | Dedicated 5.00, 5.02, 5.10, and 5.50 stage |
| [ps5-hen](https://github.com/cragson/ps5-hen) | `699870d16ca4cd5cd70fc88f8cf61b3953d4a292` | Built from supplied source | Legacy engine for the exact 1.00-4.51 profile set above |
| [ShadowMountPlus](https://github.com/drakmor/shadowMountPlus) | `98c524890176c7b474fcb03387bec88c4a559861` | Built and embedded as `hyperhen_automount.elf` | Exclusive common automount, image/title discovery, package redirect, and fakelib lifecycle |
| [onionHEN DPI v2](https://github.com/OnionBuddies/onionHEN-dpiv2-plugin) | `4e80389a87dd709f5f7ba316cbaa23d05a3cafc3` | Built and embedded as HyperDPI | Browser upload, queue, staged package, and progress service |
| [onionHEN Plugin SDK](https://github.com/OnionBuddies/onionHEN-plugin-sdk) | `0cf4346536705fb271ba0eb2226d97ebcb23559c` | Statically linked into HyperDPI | Stable client ABI and HyperHEN plugin-service transport |
| pLinux5-NG | Supplied source archive, SHA-256 `316e25ecd25d021ba0318768ca623c2d3891a78c73c4cd7ecd038c259f1ebb64` | Built into HyperLinux | High-firmware Linux boot route and hypervisor handoff |
| [ps5-linux-loader 2.5](https://github.com/ps5-linux/ps5-linux-loader) | Supplied archive, SHA-256 `610fa2e1da7e131b4365c2a6e06961b79dc208da89ad6102f706777386911ecb` | Built into HyperLinux | Linux kernel/initrd loader and upstream penguin artwork |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Build dependency | External toolchain | Prospero compiler, headers, runtime, and CMake integration |
| [elfldr](https://github.com/ps5-payload-dev/elfldr) | Runtime dependency | External; not shipped | Initial payload transport on port 9021 |

The exact snapshot notes and local integration boundaries are kept in each
component's `third_party/*/HYPERHEN_VENDOR_LOCK.md` file. Supplied reference
ELFs are used only for comparison when the matching vendor lock says so; the
release build consumes source-built artifacts.

## Libraries and technical foundations

| Project | Use |
| --- | --- |
| [libhijacker](https://github.com/astrelsky/libhijacker) | Process hijacking and kernel read/write support |
| [NineS](https://github.com/buzzer-re/NineS) | ShellUI injection foundation |
| [cJSON](https://github.com/DaveGamble/cJSON) | Configuration and JSON parsing |
| [7-Zip LZMA SDK](https://www.7-zip.org/sdk.html) | Bootstrapper compression and unpacking |
| [miniz](https://github.com/richgel999/miniz) | ZIP and cheat-data decompression |
| [Keystone Engine](https://www.keystone-engine.org/) | ShnExt assembly support |
| BearSSL `79c060eea3eea1257797f15ea1608a9a9923aa6f` | HyperCore cryptographic dependency |
| LibTomCrypt `b96e96cf8b22a931e8e91098ac37bc72f9e2f033` | HyperCore cryptographic dependency |
| ISA-L Crypto `07d0b253843ace996aa5a44f538c9b83b085aba6` | HyperCore cryptographic dependency |

The cheat subsystem also carries the AES, Base64, SHA-256, and miniz sources
under `third_party/cheat_support`. The ShellUI and NID resolver use the vendored
SHA-1 implementations present in the repository.

## Referenced community work

The following projects are not embedded as independent runtime payloads but
informed specific compatible behavior or provide optional data/services:

- [ps5-payload-manager](https://github.com/itsplk/ps5-payload-manager) by
  itsplk: listener recovery and socket rebind behavior after Rest Mode.
- [HEN-Cheats-Collection](https://github.com/TeeKay87/HEN-Cheats-Collection)
  by TeeKay87: optional community cheat catalog used by cheat sync.
- [PHU Games Tools](https://github.com/ArkSama) by ArkSama: FPS sampling
  reference used by the in-game overlay.
- [onionHEN-ftpsrv-plugin](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin):
  optional external FTP plugin. It is not embedded in `HyperHEN.elf`.

## Embedded service responsibilities

### HyperCore

HyperCore is produced locally from the pinned kstuff-a53 tree. The build joins
the kstuff-lite base, A53/PPR loader work, and the BackPork monitor into one
source-built intermediate ELF. HyperHEN does not accept the old precompiled
artifact found in the upstream archive.

### HyperHEN Automount

ShadowMountPlus is built as the single active automount owner. Before HyperCore
starts, the bootstrapper claims the kstuff automount sentinel so the native
kstuff scanner and integrated BackPork monitor do not compete with it. If the
automount stage cannot initialize, ownership is released and later dependent
startup is stopped.

The optional SceShellCore bridge is used only for exact verified profiles. An
offset-free lifecycle watcher provides the shared pre-exec event path where the
bridge is unavailable; it does not invent or approximate missing offsets.

### Daemons and Toolbox

The utility daemon starts before HyperCore on the modern route. Once the
firmware engine and automount are ready, the critical daemon starts the ShellUI
injection and Toolbox. The two-daemon design keeps utility operations separate
and lets the critical service supervise recovery.

The Toolbox manages configuration, plugins, user payloads, cheats, overlays,
network services, account tools, cooling, mount options, and the one-shot Linux
handoff. Runtime settings use one versioned INI schema shared by the UI and
services.

### Plugin runtime

Standalone plugin ELFs and compatible descriptor-bearing containers live in
`/data/HyperHEN/plugins`. HyperHEN validates the container identity and embedded
ELF separately, then launches only the embedded ELF through its private loader.
Game-process PRX/SPRX modules use a separate catalog and exact title targeting.

### HyperDPI

HyperDPI is built from pinned GPL source and launched from its embedded image;
no sidecar DPI ELF is required. Its listeners are disabled by default because
the upstream protocol has no client authentication. Enable it only on a trusted
local network.

### HyperLinux

HyperLinux is deliberately isolated from an already active HyperCore. The
Toolbox writes a one-shot marker, the user performs a full reboot, and the next
HyperHEN launch consumes that marker before normal HEN services start.

Exact HyperLinux profiles are 3.00, 3.10, 3.20, 3.21, 4.00, 4.02, 4.03, 4.50,
4.51, 5.00, 5.02, 5.10, 5.50, 6.00, 6.02, 6.50, 7.00, 7.01, 7.20, 7.40,
7.60, 7.61, plus the experimental 13.20 path.

## Runtime paths and interfaces

| Path or interface | Purpose |
| --- | --- |
| `/data/HyperHEN/config.ini` | Primary versioned configuration |
| `/user/data/HyperHEN/config.ini` | User-data configuration mirror |
| `/data/HyperHEN/plugins/` | Standalone plugin ELFs and compatible containers |
| `/data/HyperHEN/payloads/` | User-supplied payload ELFs |
| `/data/HyperHEN/cheats/` | JSON, SHN, MC4, and ShnExt cheat sources |
| `/data/HyperHEN/sprx/catalog.ini` | Title-targeted PRX/SPRX catalog |
| `/data/HyperHEN/HyperHEN.log` | Main runtime log |
| `/data/HyperHEN/HyperHEN_crash.log` | Preserved crash and backtrace log |
| `/system_tmp/hyperhen/ipc/plugin_service` | Plugin-service IPC endpoint |
| TCP `9021` | External first-hop loader supplied by the exploit environment |
| TCP `9020` | Private HyperHEN loader used after bootstrap |
| TCP `12800` | Default HyperDPI WebUI port when explicitly enabled |

## Build and reproducibility

The release build uses the PS5 Payload SDK, CMake, Ninja, Clang/LLVM, GNU Make,
NASM or Yasm, and `xz`/`lzma`. `scripts/build.sh` validates the vendored inputs,
builds the firmware engines and services from source, assembles the bootstrapper,
compresses it, writes its size and SHA-1 metadata, and links the final wrapper.

```shell
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

The normal output is `build/bin/HyperHEN.elf`. Intermediate embedded artifacts
remain under `build/vendor/`. See the root README for the full output table and
test commands.

## License boundaries

HyperHEN is distributed under GNU GPL-3.0. Vendored third-party files retain
their own copyrights, licenses, and notices. HyperLinux contains AGPL-3.0 work
from pLinux5-NG and retained GPL-3.0 work from ps5-linux-loader. Cryptographic
subcomponents inside HyperCore retain their individual license files.

The supplied Byepervisor Reborn, Byepervisor 5.xx, and ps5-hen snapshots do
not contain standalone license files at their vendored roots. The supplied
Keystone archive likewise lacks matching license metadata in this tree. Their
upstream redistribution terms must be confirmed and the appropriate notices
added before treating this source tree as a legally complete public release.
See [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

The project is unofficial, is not affiliated with Sony Interactive
Entertainment, and is intended for research and homebrew use on hardware owned
by the user. There is no warranty.
