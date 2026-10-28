<p align="center">
  <img src="assets/logo.png" alt="HyperHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>HyperHEN 0.9.3</b><br/>
  An all-in-one HEN and Toolbox for PlayStation 5<br/>
  <b>Coded by MICHELE MEDIA</b>
</p>

# HyperHEN 0.9.3

HyperHEN 0.9.3 brings together the dashboard, native tools, firmware routing,
localization and stability work completed since the public 0.9.2 release.
One entry ELF coordinates the selected engine, Automount and the Toolbox;
HyperDeck adds a Media-tab dashboard for the integrated tool engines.

Console Information now uses the **HyperHEN v0.9.3** name instead of the old
OnionHEN label. The product version is also updated in the welcome
notification, Toolbox/About and HyperDeck.

## What's new compared with public 0.9.2

The comparison below uses the published
[0.9.2 tag](https://github.com/Michele-M-Media/HYPERHEN/tree/HYPERHEN_V0_9_2).
It covers the complete post-release integration work, including additions that
were developed before the final 0.9.3 stability and branding updates.

- **HyperDeck Media dashboard:** Game Compressor, Web File Manager,
  CheatRunner, PS5Upload and package installation are brought together in one
  interface. Media-icon registration and Dashboard handoff now share the
  post-pipeline readiness path across the declared routes, including legacy
  and 7.xx profiles.
- **Expanded Advanced Debug catalog:** Firmware Spoof, NP Fake Sign-in,
  NanoDNS, Chukei DNS, zFTPd/zHTTP and firmware-gated Garlic Save Manager
  controls extend the original service switches. Component-specific
  availability checks still apply.
- **Legacy and 5.xx engine selectors:** exact legacy profiles use PS5HEN, with
  a HyperCore choice on the supported 3.xx/4.xx entries. On 5.xx, choose
  Bypervisor or HyperCore. The dedicated Bypervisor selection skips
  HyperCore, A53/PPR and BackPork, then continues to Automount and Dashboard
  services. The HyperCore choice follows the compatibility path directly.
- **Toolbox continuation on legacy firmware:** utility, daemon and Toolbox
  services now follow the selected engine and Automount readiness checks,
  instead of ending after the legacy HEN/Automount stage.
- **Updated exact routing:** 9.40 and 12.70 have dedicated startup inputs.
  Profiles 13.40/13.42 move from utility-only operation to a deliberately
  limited Kstuff-lite, Automount-lite and Dashboard/Toolbox route. That lite
  route keeps backport/fakelib, emulator replacement and automatic AMPR
  features disabled.
- **ShadowMountPlus 1.7 beta9:** standard and lite Automount variants add
  stronger live-owner, route and readiness checks while preserving existing
  configuration and explicit LAN binding. The default API binding remains
  loopback.
- **Selectors and additional notifications follow the console language:**
  startup, readiness, handoff and legacy-path messages extend the existing
  Toolbox localization. Notification rendering was checked for all 31 system
  language values, using 28 catalogs and their aliases/fallbacks.
- **Safer Toolbox startup and recovery:** manual WebKit closure, a continuous
  two-second quiet interval and a fresh pre-attach process check reduce
  browser/injection overlap. ShellUI replacement events are handled separately
  from actual standby/resume, improving recovery after payload launches from
  Advanced Debug or HyperDeck.
- **Current branding and an additional experimental loader input:** product
  interfaces consistently report v0.9.3, and Console Information uses the
  HyperHEN name while retaining the firmware string. A dedicated experimental
  13.60 Linux-loader input is kept separate from the common clean-boot loader;
  it is not a claim of hardware certification for that experimental profile.

## Features

- **Legacy engine selector:** exact 1.xx–4.xx profiles use the PS5HEN path;
  the HyperCore choice is enabled only on the exact supported 3.xx/4.xx
  profiles. HyperCore remains unavailable below 3.00.
- **5.xx engine selector:** choose Bypervisor or HyperCore on the declared
  5.xx profiles. Selectors and notifications follow the system language.
- **HyperDeck Media entry:** opens the integrated tool dashboard and its native
  engine launchers.
- **Dashboard Toolbox entry:** the HyperHEN entry remains connected to the
  Toolbox, separately from HyperDeck.
- **Automount beta9 integration:** shared mount lifecycle with standard and
  firmware-specific lite paths, using the engine inputs retained from R29.
- **Safer Toolbox startup:** browser-exit gating and improved ShellUI lifecycle
  handling carry the R29 correction into the 0.9.3 release.

The existing Toolbox services, plugins and external modules, cheats,
performance display, Remote Play pairing, user payload management/auto-start,
HyperDPI browser installer and common clean-boot Linux launcher remain part
of the project. Kernel logging on TCP 3232, FTP on TCP 2121 and the debug ELF
loader on TCP 9025 were already available in the public 0.9.2 release.

## Exact firmware profiles

These are the **67 exact entries** declared by the firmware router. A version
between listed entries is not an implicit alias. The legacy and 5.xx selectors
choose the appropriate engine within their declared profile families. The
profile count itself already existed in 0.9.2; 0.9.3 changes several routes
and the services that continue after them.

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
HyperCore remains unavailable below 3.00. Runtime checks still decide whether
each selected component can start; a table entry is not hardware certification.

## Running the release

Download [HyperHEN-v0.9.3.elf](https://github.com/Michele-M-Media/HYPERHEN/releases/download/HYPERHEN_V0_9_3/HyperHEN-v0.9.3.elf)
from the release assets. Perform a full reboot
before switching from an earlier ELF so the old ShellUI module is replaced.
Launch the payload through your existing compatible environment, close WebKit
manually if it is open, and allow the Toolbox to finish loading. The startup
guard waits for the browser content process to exit and then for a two-second
quiet period.

The shared guard covers all 67 declared profiles from 1.00 to 13.60. Declared
profile coverage and local checks are not a claim of console testing on every
firmware. The R29 startup correction was confirmed on the reported 9.05 tester.
The existing limited-hook profiles remain 1.05, 1.10, 1.11, 1.13 and 4.02.

## Source and build

Read [BUILD_RELEASE.md](BUILD_RELEASE.md) for the release-compatible Linux/WSL2
build. It rebuilds the first-party interfaces while retaining the established
engine inputs. Prebuilt dependencies, their checksums and upstream notices
remain explicit; this is not an all-dependencies-from-source distribution.

The complete source ZIP includes the retained build inputs. For a Git checkout,
download [HyperHEN-v0.9.3-build-inputs.zip](https://github.com/Michele-M-Media/HYPERHEN/releases/download/HYPERHEN_V0_9_3/HyperHEN-v0.9.3-build-inputs.zip)
from the same release and extract
it at the repository root before running the helper.

HyperHEN integration and maintenance are by **MICHELE MEDIA**. Project source
uses [GPLv3](LICENSE); upstream files retain their own licenses and notices.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the component boundaries.
The [technical overview](docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md) is retained
as a historical 0.9.2 inventory; its old routing table is not the current
0.9.3 compatibility description.

Release assets also include `HyperHEN-v0.9.3-source.zip`, `SHA256SUMS.txt` and
the verification report. Check the download digest against `SHA256SUMS.txt`.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the changes and test scope. If you
report a failure, include the exact firmware, ELF version and a klog captured
from the same boot.
