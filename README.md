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

HyperHEN v0.9.3 combines exact firmware routing, the source-built HyperCore,
one common automount owner, firmware-gated Toolbox services, and a fully
HyperHEN-branded native Settings interface. The visual system uses a unified
64 px icon family and a progressive APNG HyperCore emblem. Firmware image
decoders that do not play APNG display its complete first frame, so animation
never gates the UI.

### What's new in 0.9.3

- **HyperDeck Media dashboard** with Game Compressor, Web File Manager,
  CheatRunner, PS5Upload and package installation; expanded Advanced Debug
  controls include DNS tools, firmware spoof, NP Fake Sign-in, zFTPd/zHTTP
  and firmware-gated Garlic Save Manager.
- **Firmware-aware engine choices** on legacy and 5.xx profiles, Toolbox
  continuation on legacy routes, dedicated 9.40/12.70 inputs and a restricted
  Kstuff-lite route for 13.40/13.42. HyperCore remains unavailable below 3.00.
- **ShadowMountPlus 1.7 beta9**, stronger startup ownership/readiness checks,
  and engine selectors plus additional notifications that follow the console
  language while retaining the existing Toolbox translations.
- **Toolbox startup and recovery improvements:** close WebKit manually, then
  allow the two-second quiet interval before injection. ShellUI lifecycle
  handling also improves recovery after payload launches.
- **Consistent v0.9.3 branding**, including Console Information, and a
  dedicated experimental 13.60 Linux-loader input.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the complete comparison with the
previous public 0.9.2 release, including the dashboard/tool work completed
before the final stabilization update.

<br>

# Features

HyperHEN is a practical homebrew stack for jailbroken PS5 consoles.

The combined HyperCore has console-log validation on firmware **5.00**. Its
additional firmware profiles are compatibility data, not proof of runtime
validation. Exact modern profiles from 6.00 through 11.40 use A53/PPR except
for the dedicated 9.40 route. The exact 11.60–12.60 entries use a reduced
HyperCore route with PPR-dependent behavior disabled; 12.70 has dedicated
inputs. Unsupported or non-exact profiles fail closed.

### Exact firmware profiles

These are the **67 exact entries** declared by the router. The profile count
already existed in 0.9.2; 0.9.3 updates several routes and their continuation
services. Versions between listed entries are not implicit aliases.

<!-- PROFILE_TABLE_BEGIN -->
| Route | Count | Exact firmware versions | Startup behavior |
| --- | ---: | --- | --- |
| Legacy selector | 25 | 1.00, 1.01, 1.02, 1.05†, 1.10†, 1.11†, 1.12, 1.13†, 1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00, 3.10, 3.20, 3.21, 4.00, 4.02†, 4.03, 4.50, 4.51 | PS5HEN; HyperCore choice enabled only on the exact 3.xx/4.xx entries. Automount and Toolbox follow the selected engine's readiness checks. |
| 5.xx selector | 4 | 5.00, 5.02, 5.10, 5.50 | Bypervisor or HyperCore, then Automount and Toolbox after readiness. |
| A53/PPR | 25 | 6.00, 6.02, 6.50, 7.00, 7.01, 7.20, 7.40, 7.60, 7.61, 8.00, 8.20, 8.40, 8.60, 9.00, 9.05, 9.20, 9.60, 10.00, 10.01, 10.20, 10.40, 10.60, 11.00, 11.20, 11.40 | A53/PPR preparation and HyperCore, then Automount and Toolbox. |
| Dedicated 9.40 | 1 | 9.40 | Dedicated hypervisor and HyperCore inputs, then Automount and Toolbox. |
| HyperCore without PPR | 6 | 11.60, 12.00, 12.02, 12.20, 12.40, 12.60 | HyperCore with PPR-dependent behavior disabled, then Automount and Toolbox. |
| Hypervisor route | 4 | 12.70, 13.00, 13.20, 13.60 | Firmware-specific hypervisor stage, HyperCore, Automount and Toolbox. |
| Kstuff-lite | 2 | 13.40, 13.42 | HyperCore lite and restricted Automount lite, then Toolbox. |
<!-- PROFILE_TABLE_END -->

† **Limited on-demand ShellCore hooks:** 1.05, 1.10, 1.11, 1.13 and 4.02 retain
the scanner/API path where supported; full on-demand hooks are not claimed.
The legacy selector keeps HyperCore unavailable below 3.00. These source
profiles do not establish hardware testing on every console revision.

The [technical overview](docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md#firmware-routing)
retains the earlier component inventory; the table above is the current 0.9.3
profile description.

- **ShellUI Toolbox** — a settings page injected into the PS5 ShellUI
- **System preparation** — raise privileges, remount filesystems, and block the update partition
- **HyperCore** — kstuff + A53/PPR + BackPork in one embedded compatibility component, selected where enabled by the firmware route
- **Hypervisor route** — firmware-specific stages for 9.40, 12.70, 13.00, 13.20, and 13.60; the selected stage's completion contract is checked before HyperCore continues
- **Legacy HEN route** — internal engine with exact profiles for 1.00–4.51; the selector offers PS5HEN, plus HyperCore on the exact 3.xx/4.xx entries, before Automount and Toolbox continuation
- **5.xx Hypervisor route** — Bypervisor is a separate choice for 5.00, 5.02, 5.10, and 5.50; it skips HyperCore/A53/PPR/BackPork and continues to Automount and Dashboard services
- **11.60/12.xx compatibility route** — exact 11.60, 12.00, 12.02, 12.20, 12.40, and 12.60 profiles start HyperCore without PPR, then require the runtime core check before Automount, Toolbox, plugins, or HyperDPI can continue; 12.70 uses dedicated inputs
- **Limited 13.xx route** — 13.40 and 13.42 use restricted Kstuff-lite, Automount-lite and Dashboard/Toolbox; backport/fakelib, emulator replacement and automatic AMPR features remain disabled
- **Plugin runtime** — discovers, validates, starts, stops, and removes descriptor-bearing plugin ELFs
- **Built-in debug services** — Toolbox switches for `klogsrv` (3232), `ftpsrv` (2121), and the LAN ELF loader (9025), with process-aware start/stop handling
- **HyperHEN Automount** — ShadowMountPlus 1.7 beta9 standard/lite engines embedded as the common post-HEN stage for image, title, USB, and fakelib mount lifecycle
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
- **HyperDeck** — a Media-tab dashboard with Game Compressor, Web File Manager, CheatRunner, PS5Upload and package installation; the HyperHEN dashboard entry remains connected to the Toolbox
- **Expanded Advanced Debug** — Firmware Spoof, NP Fake Sign-in, NanoDNS, Chukei DNS, zFTPd/zHTTP and firmware-gated Garlic Save Manager controls
- **System-language selectors and notifications** — additional startup/status messages follow the console language while existing Toolbox translations and project credits are retained
- **Startup/recovery checks** — manual WebKit closure, a two-second quiet interval and replacement-instance handling improve Toolbox handoff after payload launches
- **Product version** — welcome, Toolbox/About, HyperDeck and Console Information report HyperHEN v0.9.3 while retaining the firmware information

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
7.40, 7.60, 7.61, plus the experimental 13.20 and dedicated 13.60 paths.
Other firmware revisions fail closed; the experimental inputs are not hardware
certification of those profiles.

<br>

# Run

### Requirements

1. A jailbroken PS5
2. An external [PS5 `elfldr`](https://github.com/ps5-payload-dev/elfldr) listening on port **9021** for the first hop

### Load HyperHEN

1. Run the kernel exploit and start the external `elfldr` service.
2. After a full reboot when changing versions, send the downloaded `HyperHEN-v0.9.3.elf` or locally built `HyperHEN.elf` through the loader your exploit host provides.
3. HyperHEN uses the exact firmware route and opens the legacy/5.xx engine selector where supported. On FW 13.00, 13.20, or 13.60, follow the HyperHEN Hypervisor notification: enter Rest Mode once and resume. Follow the component-specific notification on dedicated 9.40/12.70 routes; on exact 11.60–12.60 profiles, wait for the compatibility-mode notification and runtime check.
4. Close WebKit manually if it is open, allow the two-second quiet interval, then open PS5 Settings and enter the HyperHEN Toolbox. FW 13.40 and 13.42 now use the restricted lite route rather than utility-only mode.

Startup is sequential. After the first hop, HyperHEN uses its own
`hyperhen_elfldr.elf` on port **9020** and keeps **9021** only as a fallback.

```text
HyperHEN.elf → bootstrapper → exact firmware route → HyperHEN Automount → daemon/Toolbox where available
```

External plugins are discovered and started by the daemon after the selected
runtime is ready, subject to component-specific availability checks.

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

**Advanced Debug** includes three independent switches: the kernel-log server
on TCP 3232, the FTP server on TCP 2121, and the LAN ELF loader on TCP 9025.
HyperHEN installs the two verified upstream payload images under
`/data/HyperHEN/tools`, builds the LAN loader from source, launches each through
its private loader, and records the returned process identity so the same
switch can stop only the service it started. These unauthenticated development
services must be used only on a trusted local network.

The separate
[`onionHEN-ftpsrv-plugin`](https://github.com/OnionBuddies/onionHEN-ftpsrv-plugin)
remains supported as an optional plugin when a separately configurable FTP
instance is preferred. Do not enable two FTP listeners on the same port.

The original service switches are retained. Additional Advanced Debug controls
integrate Firmware Spoof, NP Fake Sign-in, NanoDNS, Chukei DNS, zFTPd/zHTTP and
Garlic Save Manager, with component- and firmware-specific availability checks.

### Mount integration

HyperHEN embeds ShadowMountPlus 1.7 beta9 and starts it only
after the selected firmware route is ready. It is the sole active automount
owner. Before HyperCore starts, the bootstrapper claims the kstuff automount
sentinel; HyperCore's native scanner and integrated BackPork monitor then stay
paused while HyperHEN Automount owns the lifecycle. Startup checks verify live
ownership and the selected firmware/engine readiness before dispatch, avoiding
conflicting mount owners and false-ready announcements. Existing configuration
and explicit LAN binding are preserved; the default API binding is loopback.

The optional SceShellCore bridge is enabled only for exact offsets present in
the supplied ShadowMountPlus table. Missing revisions are not approximated;
the base scanner/mount service remains separate from those optional hooks.
Profiles 1.05, 1.10, 1.11, 1.13 and 4.02 retain an explicitly limited
scanner/API path where supported, without inventing ShellCore offsets or
claiming full on-demand hooks. Other initialization failures stop startup
instead of advertising complete readiness.

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

For the tagged 0.9.3 release-compatible build, use the new helper:

```shell
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
bash scripts/build_release.sh --jobs 6
```

A Git checkout also needs
[HyperHEN-v0.9.3-build-inputs.zip](https://github.com/Michele-M-Media/HYPERHEN/releases/download/HYPERHEN_V0_9_3/HyperHEN-v0.9.3-build-inputs.zip)
extracted at the repository root. The complete source ZIP already includes
these checksum-verified inputs. This recipe rebuilds the first-party interfaces
and uses the retained engine inputs from the tested R29 build; output is
`build-release/bin/HyperHEN.elf`. See [BUILD_RELEASE.md](BUILD_RELEASE.md) for
the complete SDK/runtime requirements and input boundaries.

The existing broader developer source-build recipe remains:

```shell
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

That script validates the vendored dependencies, builds HyperCore and HyperHEN
Automount locally, and then builds the embed chain in the required order. Its
source outputs are separate from the retained inputs used by the 0.9.3
release-compatible helper above.

The existing Docker developer recipe is:

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
3. If the Toolbox does not appear, close WebKit manually, wait for the selected engine → Automount → daemon startup to finish, and check the logs. FW 13.40/13.42 use the restricted Kstuff-lite / Automount-lite route with Toolbox support.
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
- [elfldr](https://github.com/ps5-payload-dev/elfldr) — John Törnblom; external first-hop loader on port 9021. HyperHEN also builds isolated private (9020) and debug-LAN (9025) loader variants from its retained in-tree loader source
- [klogsrv](https://github.com/ps5-payload-dev/klogsrv) — John Törnblom, with current port/quiet argument work by Al-Azif; the verified upstream PS5 ELF is embedded for the Toolbox log-server switch
- [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) — John Törnblom, with current-tree contributions from EchoStretch, Al-Azif, Matthew Vanek, and Gezine; the verified upstream PS5 ELF is embedded for the Toolbox FTP switch
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

#### Advanced Debug payload additions

- [PS5 Firmware Spoof](https://github.com/illusionyy/ps5-fw-spoof) — **illusionyy**.
- [NP Fake Sign-in](https://github.com/earthonion/np-fake-signin) — **earthonion**; its upstream acknowledgements also credit John Törnblom's [offact](https://github.com/ps5-payload-dev/offact) for offline activation.
- [nanoDNS](https://github.com/drakmor/nanoDNS) — **drakmor**.
- [Chukei DNS](https://github.com/Al-Azif/chukei-dns) — **Al Azif (Al-Azif)**.
- [zFTPd + zHTTP](https://github.com/seregonwar/zftpd) — **Seregon (SeregonWar)**.
- [Garlic SaveMgr](https://github.com/earthonion/garlic-savemgr) — **earthonion**.

#### HyperDeck components

- [PS5 Game Compressor](https://github.com/juma-sayeh/PS5-Game-Compressor) — **Juma Sayeh (juma-sayeh)**.
- [PS5 Web File Manager](https://github.com/owendswang/ps5-web-file-manager) — **owendswang**.
- [CheatRunner](https://github.com/notmaj0r/CheatRunner) — **notmaj0r (maj0r)**.
- [PS5Upload](https://github.com/phantomptr/ps5upload) — **PhantomPtr**.
- [websrv and the PKG installation helper](https://github.com/ps5-payload-dev/websrv) — **John Törnblom and the ps5-payload-dev contributors**; the web-service foundation and native package-install helper used by HyperDeck.

The exact provenance, revisions, inclusion form, firmware scope, and license
boundary for every major component are recorded in the
[technical overview](docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md) and the
per-component `HYPERHEN_VENDOR_LOCK.md` files under `third_party/`.

### Testers

- [**@oG_LuckyXmods**](https://x.com/oG_LuckyXmods) — special thanks for the extensive help testing HyperHEN.

Thanks as well to everyone else who tested, researched, or sent usable feedback.

<br>

# License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses and notices;
`third_party/hyperlinux` retains its AGPL-3.0 and GPL-3.0 terms.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before redistribution.

> HyperHEN is an unofficial homebrew project and is not affiliated with Sony Interactive Entertainment.
> Use it only on hardware you own and at your own risk. No warranty is provided.
