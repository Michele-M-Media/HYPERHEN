<p align="center">
  <img src="assets/logo.png" alt="HyperHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>HyperHEN</b><br/>
  An all-in-one HEN and Toolbox for PlayStation 5
</p>

<p align="center">
  <b><a href="#features">Features</a></b>
  ·
  <b><a href="#run">Run</a></b>
  ·
  <b><a href="#build">Build</a></b>
  ·
  <b><a href="#configuration">Configuration</a></b>
  ·
  <b><a href="docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md">Technical overview</a></b>
  ·
  <b><a href="#credits">Credits</a></b>
</p>

---

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

<p align="center">
  HyperHEN is a modular payload stack for the PS5. One entry ELF prepares
  the system, injects a ShellUI Toolbox,<br/>and then runs overlays, cheats,
  payloads, and background services.
</p>

The v0.5 visual system uses native Settings XML, a unified 64 px icon family,
and a progressive APNG HyperCore emblem. Firmware image decoders that do not
play APNG display its complete first frame, so animation never gates the UI.

<br>

# Features

HyperHEN is a practical homebrew stack for jailbroken PS5 consoles.

The combined HyperCore has console-log validation on firmware **5.00**. Its
additional firmware profiles are compatibility data, not proof of runtime
validation; unsupported or non-exact A53/PPR profiles fail closed before
kstuff starts.

- **ShellUI Toolbox** — a settings page injected into the PS5 ShellUI
- **System preparation** — raise privileges, remount filesystems, and block the update partition
- **HyperCore** — source-built kstuff + A53/PPR + BackPork in one embedded component, loaded by default
- **Hypervisor route** — source-built Byepervisor Reborn stage embedded for FW 13.00, 13.20, and 13.60; it completes through Rest Mode before HyperCore continues
- **Legacy HEN route** — source-built internal engine with exact profiles for 1.00–4.51; selected automatically before the common automount stage
- **5.xx Hypervisor route** — source-built Byepervisor stage for 5.00, 5.02, 5.10, and 5.50; selected automatically before HyperCore
- **Plugin runtime** — discovers, validates, starts, stops, and removes descriptor-bearing plugin ELFs
- **PS5 FTP server** — available as the optional external `onionHEN-ftpsrv-plugin`
- **HyperHEN Automount** — source-built ShadowMountPlus engine embedded as a common post-HEN stage for image, title, USB, and fakelib mount lifecycle
- **Remote Play pairing** — enable the native PS5 Remote Play service, generate a pairing PIN, and register a client from the Network section
- **User payload manager** — start and stop user-provided `.elf` payloads, with optional auto-start
- **Game overlay** — an in-game bar for FPS, CPU, GPU, RAM, temperatures, and network info
- **Cheat engine** — local JSON, SHN, MC4, and ShnExt files that can be toggled at runtime
- **HyperDPI** — source-built browser package installer embedded in HyperHEN, with dynamic Toolbox controls
- **HyperLinux Boot** — source-built pLinux5-NG/ps5-linux-loader engine embedded behind a penguin entry in Advanced Debug; a one-shot clean-boot handoff keeps it isolated from HyperCore
- **Startup voice** — plays the embedded “HyperHEN START!” cue after the selected firmware engine is ready; enabled by default and optional in the Toolbox
- **Console tools** — account activation, external HDD, Title IDs, fan control, shortcuts, and game options
- **App jailbreak** — allowlisted homebrew can ask the daemon for extra privileges through a sandbox FIFO
- **Resilient runtime** — the critical daemon and utility daemon run apart; the main daemon can restart the utility
- **Shared configuration** — the Toolbox and daemons use one versioned `config.ini`

HyperHEN does not include a kernel exploit. The first hop still needs an
external `elfldr` on **9021**. After that, HyperHEN starts its own private
loader on **9020** for later ELF and user-payload launches.

### HyperLinux clean-boot mode

Open **Advanced Debug → HyperLinux Boot** to arm the embedded Linux loader.
Because the upstream Linux boot path is incompatible with kstuff already being
active, the button deliberately does not launch it in the current session.
Fully reboot the console and start HyperHEN once: the bootstrapper consumes the
one-shot marker and hands off to HyperLinux before HyperCore starts.

A prepared USB must contain `bzImage` and `initrd.img` either at its root or in
`PS5/Linux/`. Rest Mode USB power must be set to **Always** and HDMI Device Link
must be disabled. Exact profiles are 3.00, 3.10, 3.20, 3.21, 4.00, 4.02, 4.03,
4.50, 4.51, 5.00, 5.02, 5.10, 5.50, 6.00, 6.02, 6.50, 7.00, 7.01, 7.20,
7.40, 7.60, 7.61, plus the experimental 13.20 path. Other firmware revisions
fail closed.

<br>

# Run

### Requirements

1. A jailbroken PS5
2. An external [PS5 `elfldr`](https://github.com/ps5-payload-dev/elfldr) listening on port **9021** for the first hop

### Load HyperHEN

1. Run the kernel exploit and start the external `elfldr` service.
2. Send `HyperHEN.elf` through the loader your exploit host provides.
3. HyperHEN selects the exact internal firmware engine automatically. On FW 13.00, 13.20, or 13.60, follow the HyperHEN Hypervisor notification: enter Rest Mode once and resume. Then wait for HyperCore and the main daemon.
4. Open PS5 Settings and enter the HyperHEN Toolbox.

Startup is sequential. After the first hop, HyperHEN uses its own
`hyperhen_elfldr.elf` on port **9020** and keeps **9021** only as a fallback.

```text
HyperHEN.elf → bootstrapper → exact firmware route → HyperHEN Automount → daemon/Toolbox where available
```

External plugins are discovered and started by the daemon after HyperCore is
ready.

### Plugin compatibility

The Toolbox **Plugins** page manages both native HyperHEN plugin ELFs and
etaHEN-compatible `.plugin` containers installed in
`/data/HyperHEN/plugins`. HyperHEN validates the container identity and its
embedded ELF separately, launches only the embedded ELF through the private
loader, and preserves the plugin ID as the spawned process name. The existing
start, stop, reload, delete, and next-boot auto-start controls apply to both
formats.

Game-process `.prx` and `.sprx` modules remain isolated from standalone plugin
processes. Their catalog is managed from **Toolbox → SPRX**, using
`/data/HyperHEN/sprx/catalog.ini` with exact title-ID targeting. A structurally
valid container does not guarantee compatibility with every game build or
third-party plugin API.

### FTP server

FTP is provided by the separate
[`onionHEN-ftpsrv-plugin`](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin).
Install it as `/data/HyperHEN/plugins/FTPS00001.elf`; HyperHEN discovers and
starts the plugin, and its dynamic settings page provides enable, port, and
restart controls. The plugin retains upstream `ftpsrv` commands such as
`KILL`, `SELF`, `SCHK`, `MTRW`, and `AUTHID` where supported.

### Mount integration

HyperHEN embeds ShadowMountPlus as `hyperhen_automount.elf` and starts it only
after the selected firmware route is ready. It is the sole active automount
owner. Before HyperCore starts, the bootstrapper claims the kstuff automount
sentinel; HyperCore's native scanner and integrated BackPork monitor then stay
paused while HyperHEN Automount owns the lifecycle. If automount initialization
fails, ownership is released instead of leaving two mount engines active.

The optional SceShellCore bridge is enabled only for exact offsets present in
the supplied ShadowMountPlus table. Missing revisions are not approximated;
the base scanner/mount service remains separate from those optional hooks.
When the verified bridge is unavailable, the shared lifecycle watcher uses an
offset-free SysCore child event to prepare package redirects and fakelib layers
inside the pre-exec window. This supplies the legacy backport path on 1.05,
1.10, 1.11, 1.13, and 4.02 without inventing ShellCore offsets; verified
profiles continue to use the earlier firmware-specific bridge.

### HyperDPI (network package installer)

HyperDPI is compiled from the pinned GPL source, embedded in the main payload,
and launched from memory after the HyperHEN daemon is ready. No
`DPIV00001.elf`, sidecar download, or file under `/data/HyperHEN/plugins` is
required. Its dynamic Toolbox page provides enable, API port, WebUI port, and
restart controls. The browser retains chunked uploads, queue management,
staged-file reuse, and SSE progress. The default WebUI URL is
`http://<console>:12800`.

The listeners are disabled on first boot. HyperDPI has no client
authentication and listens on all console interfaces when enabled, so use it
only on a trusted local network and stop it when finished. Settings are stored
in `/data/HyperHEN/HyperDPI.ini`; uploads are staged under
`/user/data/HyperHEN/pkgs`.

### Remote Play

Remote Play pairing is available from **Toolbox → Network → Remote Play**.
The current account must already be activated. If it is not, HyperHEN blocks
the Remote Play page and directs you to **Toolbox → Account → Account
activation**. Once the account is activated, HyperHEN enables the Remote Play
registry setting and displays the pairing PIN. Enter that PIN in the official
Remote Play client while the page is open; HyperHEN uses the native PS5 Remote
Play service to confirm the registered device.

The feature provides pairing and registration only. Video streaming and client
transport remain handled by Sony's native Remote Play service.

### Payloads

Place standalone payloads in:

```text
/data/HyperHEN/payloads/
```

Only plain `.elf` files are supported. Auto-start can be turned on in the Toolbox;
HyperHEN remembers that choice with a matching `.auto_start` file next to the ELF.

All `.elf` filenames use the same Payload page, loader, and auto-start flow. A
recorded running instance is left running by later launch and auto-start
requests. HyperHEN plugins are a separate category and belong in
`/data/HyperHEN/plugins/`.

### Cheats

Put cheat files in one directory. Names are `TITLEID_VERSION`, with an
optional process and 8-hex source ID:

```text
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].json
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].shn
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].mc4
/data/HyperHEN/cheats/<TITLE_ID>_<VERSION>[_<PROCESS>][_<SOURCE_ID>].ShnExt
```

`PROCESS` is optional. `SOURCE_ID` is a stable physical-source discriminator.
HyperHEN loads every compatible source for the title/version/process, so
independent JSON, SHN, MC4 and ShnExt files can coexist. A source with an
explicit process is only used for that process; a source without one is
generic.

Cheats load from disk. If a file changes, HyperHEN reloads it without restarting the whole stack.

`DOWNLOAD_CHEATS` downloads a cheat catalog ZIP over HTTPS (GitHub or cnb.cool), extracts the `cheats/` tree, and copies `json/`, `shn/`, and `mc4/` files into this directory with their original names. `[cheats] mirror=auto` uses cnb.cool when the UI/system language is Simplified Chinese, otherwise GitHub.

<br>

# Build

### Dependencies

| Dependency | Purpose |
| --- | --- |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Prospero compiler, target headers, runtime, and CMake wrapper |
| CMake 3.20+ and Ninja | Configure and build the payload graph |
| Clang / LLVM | Compile the `x86_64-sie-ps5` targets |
| `lzma` or `xz` | Compress the bootstrapper |
| GNU Make, GCC/Clang, NASM/Yasm, xxd | Build the vendored HyperCore, Automount, and firmware-routed HEN/Hypervisor sources |

### Full build

```shell
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

The script validates the vendored dependencies, builds HyperCore and HyperHEN
Automount locally, and then builds the embed chain in the required order. It
never downloads or accepts a precompiled kstuff or automount ELF.

For the reproducible Docker build:

```shell
docker compose build hyperhen-build
docker compose run --rm hyperhen-build
```

### Common options

| Option | Description |
| --- | --- |
| `--build-type Debug\|Release` | Select the CMake build type |
| `--build-dir <path>` | Override the default `build/` directory |
| `--skip-unpacker` | Stop after building the bootstrapper |

Run `./scripts/build.sh --help` for the complete option list.

### Outputs

| Path | Description |
| --- | --- |
| `build/bin/HyperHEN.elf` | Final payload sent to the console |
| `build/bin/bootstrapper.elf` | Uncompressed bootstrapper |
| `build/bin/bootstrapper.elf.lzma` | Bootstrapper embedded by the final payload |
| `build/bin/daemon.elf` | Critical daemon |
| `build/bin/util.elf` | Utility daemon |
| `build/bin/shellui.elf` | Toolbox injection payload |
| `build/vendor/hypercore/kstuff.elf` | Source-built intermediate embedded as HyperCore |
| `build/vendor/automount/hyperhen_automount.elf` | Source-built ShadowMountPlus engine embedded as HyperHEN Automount |
| `build/vendor/hypervisor/hyperhen_hypervisor.elf` | Source-built Byepervisor stage embedded by the bootstrapper |
| `build/vendor/hypervisor-5xx/hyperhen_hypervisor_5xx.elf` | Source-built 5.xx Byepervisor stage embedded by the bootstrapper |
| `build/vendor/legacy-hen/hyperhen_legacy_hen.elf` | Source-built 1.xx–4.51 HEN engine embedded by the bootstrapper |
| `build/vendor/hyperlinux/hyperlinux.elf` | Source-built Linux loader embedded for the clean-boot handoff |
| `build/lib/*.a` | In-tree static libraries |

Final build outputs stay in `build/`. HyperCore's upstream Makefiles also
create intermediate objects below `third_party/kstuff-a53/`; no generated ELF
is accepted from the upstream `artifacts/` directory.

### Tests

Host tests cover settings, IPC, payload helpers, cheat parsers, relocation,
Toolbox routing, and shared platform code:

```shell
make -C source/util/tests clean
make -C source/util/tests test -j8
```

Linking the host tests needs Keystone on `KEYSTONE_PREFIX`
(default: `/opt/homebrew`).

<br>

# Configuration

HyperHEN reads and writes the same config in two places:

```text
/data/HyperHEN/config.ini
/user/data/HyperHEN/config.ini
```

Most options can be changed in the Toolbox. The format is
`schema_version=1`. If no file exists, HyperHEN writes a commented
default from [`config.ini.example`](config.ini.example).

| Key | Default | Values |
| --- | --- | --- |
| `meta.schema_version` | `1` | `1` |
| `toolbox.language` | `system` | `system`, `zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, `fr`, `de`, `it`, `es`, `pt-BR`, `pt-PT`, `pl`, `ru`, `ar`, `th`, `nl`, `fi`, `sv`, `da`, `no`, `tr`, `cs`, `hu`, `el`, `ro`, `vi`, `id`, `uk` |
| `startup.open_after_load` | `none` | `none`, `home_menu` |
| `startup.hype_voice` | `true` | `true`, `false` |
| `home_screen.show_title_ids` | `false` | `true`, `false` |
| `game_menu.show_hyperhen_options` | `true` | `true`, `false` |
| `rest_mode.resume_reinject_delay_seconds` | `10` | `0` through `255` seconds |
| `cheats.mirror` | `auto` | `auto`, `github`, `cnb` |
| `app_jailbreak.debug_notifications` | `false` | `true`, `false` |
| `cooling.fan_control` | `automatic` | `automatic`, `temperature_threshold` |
| `cooling.temperature_threshold_celsius` | `77` | `0` through `100` |
| `overlay.enabled` | `true` | `true`, `false` |
| `overlay.background` | `true` | `true`, `false` |
| `overlay.edge` | `top` | `top`, `bottom` |
| `overlay.align` | `center` | `left`, `center`, `right` |
| `overlay.font_size` | `medium` | `small`, `medium`, `large` |
| `overlay.order` | `fps,cpu,gpu,memory,ip,fan` | comma-separated `fps`, `cpu`, `gpu`, `memory`, `ip`, `fan` |
| `overlay.show_cpu` / `overlay.show_gpu` / `overlay.show_memory` / `overlay.show_fps` | `true` | `true`, `false` |
| `overlay.cpu_usage_mode` | `average` | `average`, `per_core` |
| `overlay.show_ip_address` | `false` | `true`, `false` |
| `shortcuts.cheats_menu` | `off` | `off`, `r3_l3`, `l2_triangle`, `long_options`, `long_share`, `share` |
| `shortcuts.toolbox` | `off` | `off`, `l2_r3`, `long_share`, `share` |

### Runtime data

| Path | Purpose |
| --- | --- |
| `/data/HyperHEN/payloads/` | User payload ELFs |
| `/data/HyperHEN/plugins/` | Descriptor-bearing HyperHEN plugin ELFs |
| `/data/HyperHEN/cheats/` | Cheat files |
| `/data/HyperHEN/cheats_tmp/` | Temporary HTTPS ZIP and extraction files, cleaned after sync |
| `/data/HyperHEN/HyperHEN.log` | Main runtime log |
| `/data/HyperHEN/HyperHEN_crash.log` | Preserved daemon signal and backtrace log |
| `/data/HyperHEN/HyperHEN_util_daemon.log` | Utility daemon log |

<br>

# Host tools

| Tool | Purpose |
| --- | --- |
| [`scripts/daemon_log.py`](scripts/daemon_log.py) | Stream daemon logs from the console |
| [`scripts/shutdown_hyperhen.py`](scripts/shutdown_hyperhen.py) | Shut down the HyperHEN userland stack without killing `kstuff` |
| [`scripts/ps5_cmake.sh`](scripts/ps5_cmake.sh) | Run CMake through the PS5 payload SDK |
| [`scripts/sync_dependencies.sh`](scripts/sync_dependencies.sh) | Verify that all source-pinned HyperCore inputs are present |

<br>

# Repository layout

```text
.
├── assets/                    Project artwork
├── docs/                      Architecture and technical notes
├── scripts/                   Build, dependency, and host-side helpers
├── source/                    In-tree PS5 sources
│   ├── bootstrapper/          Startup and embedded payload chain
│   ├── daemon/                Critical daemon and Toolbox injection
│   ├── util/                  Utility daemon, IPC, and cheats
│   ├── shellui/               Toolbox and ShellUI hooks
│   ├── i18n/                  Shared Toolbox / notification locale catalogs
│   ├── unpacker/              Final HyperHEN payload wrapper
│   ├── libonion_*/            Shared in-tree libraries
│   ├── common/                Shared low-level implementations
│   └── platform/ps5/stubs/    PS5 system-library link stubs
├── third_party/               Vendored dependencies, including source-pinned HyperCore
├── tools/                     Repository-side generators
└── build/                     Generated build outputs (ignored)
```

See [the architecture document](docs/arch.md) for the module and IPC map and
[the HyperCore integration note](docs/HYPERCORE_INTEGRATION.md) for source,
startup, exclusivity, and firmware boundaries. The
[components and technical overview](docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md)
records the complete firmware-routing matrix, startup contract, embedded and
external dependencies, pinned revisions, licensing boundaries, and validation
status.

<br>

# Troubleshooting

1. Before loading HyperHEN, make sure the external `elfldr` is reachable on port **9021**.
2. Make sure the payload was built for this firmware with a complete PS5 Payload SDK.
3. If the Toolbox does not appear, wait for `util → HyperCore → daemon` to finish and check the logs.
4. If HyperCore does not build, run `./scripts/sync_dependencies.sh`; it reports the exact missing vendored source.
5. When you report a problem, include firmware, exploit host, SDK version, build type, logs, and steps to reproduce.

<br>

# Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

<br>

# Credits

HyperHEN exists because of the PS5 homebrew and reverse-engineering community.

### Contributors

HyperHEN integration, product identity, firmware-route orchestration, release
engineering, and project maintenance: **MICHELE MEDIA**.

<p align="center">
  <a href="https://github.com/aydencharles/onionHEN/graphs/contributors">
    <img src="https://contrib.rocks/image?repo=aydencharles/onionHEN" alt="Upstream OnionHEN contributors"/>
  </a>
</p>

### Based on

- [OnionHEN](https://github.com/aydencharles/onionHEN) — original modular stack, retained under GPLv3 with its copyright notices and internal `onion::*` compatibility API
- [etaHEN](https://github.com/LightningMods/etaHEN) — LightningMods and contributors; source base of this tree
- [GoldHEN](https://github.com/GoldHEN/GoldHEN) — SiSTR0 and contributors; the PS4 all-in-one HEN this project takes after

### Referenced

- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch, sleirsgoevy, and contributors; Rest Mode Toolbox recovery follows its SceSysCore `NOTE_EXEC` watch for `NPXS40087` and wait for `libSceNpTrophy.sprx` / `libSceNpTrophy2.sprx`
- [ps5-payload-manager](https://github.com/itsplk/ps5-payload-manager) — itsplk; listen-socket rebind after Rest Mode (Unix IPC and TCP accept-fail self-heal) follows this project
- [HEN-Cheats-Collection](https://github.com/TeeKay87/HEN-Cheats-Collection) — TeeKay87; the community cheat collection downloaded by the built-in cheat sync
- [PHU Games Tools](https://github.com/ArkSama) — ArkSama; the in-game FPS counter follows PHU Games Tools skip-hook sampling (`/dev/dce` scanout and DMAP reads of `libSceAgcDriver`)

### Used or embedded

- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) — Prospero toolchain and headers
- [elfldr](https://github.com/ps5-payload-dev/elfldr) — first-hop loader on port 9021; not shipped in the payload
- [kstuff-a53](https://github.com/Soonniicc/kstuff-a53) — integrated source snapshot combining the kstuff-lite base with A53/PPR and BackPork
- [Byepervisor Reborn](https://github.com/PS5Dev/Byepervisor-Reborn) — supplied hypervisor-stage source integrated for its exact 13.00, 13.20, and 13.60 offsets
- Byepervisor 5.xx — supplied source integrated for its exact 5.00, 5.02, 5.10, and 5.50 profiles
- ps5-hen — supplied source integrated for its exact 1.00–4.51 profile set
- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch, sleirsgoevy, and contributors; HyperCore base
- [ppr-patch](https://github.com/drakmor/ppr-patch) — drakmor; A53/PPR integration base
- [BackPork](https://github.com/BestPig/BackPork) — BestPig; process monitor and `fakelib` mount base
- [a53-code-exec](https://github.com/cragson/a53-code-exec) — A53 technical reference; its ELF is not embedded
- [onionHEN-ftpsrv-plugin](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin) — optional external FTP server plugin based on drakmor/ftpsrv
- ShadowMountPlus — Drakmor and contributors; source-built and embedded as the exclusive HyperHEN Automount engine
- [onionHEN DPI v2 plugin](https://github.com/OnionBuddies/onionHEN-dpiv2-plugin) — GPL source base for the embedded, HyperHEN-branded HyperDPI service
- [onionHEN Plugin SDK](https://github.com/OnionBuddies/onionHEN-plugin-sdk) — pinned compatibility ABI used by the embedded HyperDPI client
- pLinux5-NG and [ps5-linux-loader](https://github.com/ps5-linux/ps5-linux-loader) — embedded HyperLinux boot engine and penguin artwork; exact firmware profiles are gated at runtime
- [libhijacker](https://github.com/astrelsky/libhijacker) — astrelsky; process hijack and kernel R/W
- [NineS](https://github.com/buzzer-re/NineS) — buzzer-re; ShellUI injection
- [cJSON](https://github.com/DaveGamble/cJSON) — JSON parsing
- [7-Zip LZMA SDK](https://www.7-zip.org/sdk.html) — unpacker decompression
- [miniz](https://github.com/richgel999/miniz) — cheat-file decompression
- [Keystone Engine](https://www.keystone-engine.org/) — ShnExt assembly support
- [BearSSL](https://www.bearssl.org/) — cryptographic support vendored at the HyperCore-pinned revision
- [LibTomCrypt](https://github.com/libtom/libtomcrypt) — cryptographic support vendored at the HyperCore-pinned revision
- [ISA-L Crypto](https://github.com/intel/isa-l_crypto) — cryptographic support vendored at the HyperCore-pinned revision

The exact provenance, revisions, inclusion form, firmware scope, and license
boundary for every major component are recorded in the
[technical overview](docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md) and the
per-component `HYPERHEN_VENDOR_LOCK.md` files under `third_party/`.

### Testers

Thanks as well to everyone else who tested, researched, or sent usable feedback.

<br>

# License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses and notices;
`third_party/hyperlinux` retains its AGPL-3.0 and GPL-3.0 terms.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistribution.

> HyperHEN is an unofficial homebrew project and is not affiliated with Sony Interactive Entertainment.
> Use it only on hardware you own and at your own risk. No warranty is provided.
