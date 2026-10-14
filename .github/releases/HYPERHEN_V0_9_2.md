# HyperHEN v0.9.2

HyperHEN has evolved into a single, firmware-aware PS5 homebrew stack. This
release brings the firmware engines, HyperCore, Automount, Toolbox, plugins,
network services, and optional boot tools together behind one entry ELF and one
strict startup pipeline.

## What's new

- Unified exact-profile firmware router with fail-closed handling for unknown
  or incomplete profiles.
- Source-built HyperCore combining the kstuff base, A53/PPR preparation, and
  the integrated backport lifecycle.
- Dedicated Byepervisor routes for the exact 5.xx profiles and for
  13.00/13.20/13.60.
- Rest Mode completion tracking on the supported 13.xx hypervisor route.
- Compatibility mode for 11.60 through the listed 12.xx profiles, with
  PPR-dependent hooks disabled instead of approximating unsupported data.
- HyperHEN Automount restored as the single common mount owner on every
  complete HEN route, including the legacy profiles.
- Stronger engine-readiness checks before Automount, Toolbox, plugins, payloads,
  or HyperDPI are allowed to continue.
- Longer A53/PPR initialization window and safer timeout/failure handling.
- HyperHEN-branded native Settings Toolbox, refreshed assets, centralized
  versioning, and updated strings across 28 language catalogs.
- Plugin management for standalone ELFs and compatible descriptor-bearing
  containers, plus a separate title-targeted SPRX catalog.
- Embedded HyperDPI browser package service with Toolbox controls.
- HyperLinux clean-boot handoff with exact-profile gating and its dedicated
  Advanced Debug entry.
- Three switchable Advanced Debug services:
  - kernel log server on TCP 3232;
  - FTP server on TCP 2121;
  - LAN ELF loader on TCP 9025.
- Process-aware start/stop handling for the debug services, isolated from the
  private HyperHEN loader on TCP 9020 and the external first-hop loader on 9021.
- Cleaner notifications, product identity, runtime markers, and crash/recovery
  behavior throughout the startup sequence.

## Firmware profiles and startup order

Firmware detection is exact. A version range below is only shorthand for the
explicitly listed profiles; nearby versions are never assumed compatible.

| Route | Exact profiles | Startup behavior |
| --- | --- | --- |
| Legacy HEN | 1.00, 1.01, 1.02, 1.05, 1.10, 1.11, 1.12, 1.13, 1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00, 3.10, 3.20, 3.21, 4.00, 4.02, 4.03, 4.50, 4.51 | Legacy engine → completion gate → HyperHEN Automount/backport lifecycle. The modern daemon and Toolbox are not started on this route. |
| Byepervisor 5.xx | 5.00, 5.02, 5.10, 5.50 | Byepervisor → HyperCore → readiness check → Automount/backport → utility, daemon, Toolbox, and optional services. |
| HyperCore A53/PPR | 6.00, 6.02, 6.50, 7.00, 7.01, 7.20, 7.40, 7.60, 7.61, 8.00, 8.20, 8.40, 8.60, 9.00, 9.05, 9.20, 9.40, 9.60, 10.00, 10.01, 10.20, 10.40, 10.60, 11.00, 11.20, 11.40 | A53/PPR preparation → HyperCore → completion marker and independent capability check → Automount/backport → modern services. |
| HyperCore compatibility | 11.60, 12.00, 12.02, 12.20, 12.40, 12.60, 12.70 | No-PPR HyperCore → completion marker and independent capability check → Automount/backport → modern services. PPR-dependent behavior remains disabled. |
| Byepervisor Reborn | 13.00, 13.20, 13.60 | Byepervisor Reborn → required Rest Mode/resume completion → HyperCore → Automount/backport → modern services. |
| Limited userland | 13.40, 13.42 | Utility layer only. HyperCore, Automount, Toolbox, plugins, payload auto-start, and HyperDPI remain disabled. |

The combined HyperCore path has console-log validation on firmware 5.00.
Additional profiles contain exact compatibility data but still require broader
hardware testing. Unsupported profiles are rejected instead of borrowing
offsets from a nearby firmware.

## Integrated services

The modern firmware routes can expose HyperHEN Tools, user payload management,
plugins, title-targeted SPRX modules, cheats, the game overlay, Remote Play
pairing, HyperDPI, HyperLinux handoff, and the new Advanced Debug services.
Network-facing development services are disabled until explicitly selected and
should be used only on a trusted local network.

HyperHEN does not include the initial kernel exploit. An already jailbroken
console and an external `elfldr` endpoint on TCP 9021 are still required for the
first hop. After bootstrap, HyperHEN attempts to use its private loader on TCP
9020.

## Credits for the new debug services

- [`klogsrv`](https://github.com/ps5-payload-dev/klogsrv): John Törnblom, with
  the current port/quiet argument contribution by Al-Azif.
- [`ftpsrv`](https://github.com/ps5-payload-dev/ftpsrv): John Törnblom, with
  current-tree contributions from EchoStretch, Al-Azif, Matthew Vanek, and
  Gezine.
- [`elfldr`](https://github.com/ps5-payload-dev/elfldr): John Törnblom.

HyperHEN integration, firmware orchestration, product identity, and release
engineering: **MICHELE MEDIA**. Every upstream component retains its original
copyright and license terms. See the repository technical overview and
third-party notices for the complete component inventory.

## One more thing

Some changes are better discovered than documented.

When the moment comes, remember these words:

### HYPERHEN START.
