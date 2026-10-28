# HyperHEN 0.9.3

HyperHEN 0.9.3 combines the work completed since the published 0.9.2 tag:
HyperDeck, expanded native-tool integrations, engine selectors, updated
firmware routes, Automount beta9, additional localized messages and ShellUI
startup/recovery improvements.

## What's new compared with public 0.9.2

### HyperDeck and native tools

HyperDeck adds a Media-tab dashboard combining **Game Compressor, Web File
Manager, CheatRunner, PS5Upload and package installation**. Media registration,
owned-instance reuse and Dashboard handoff use the common post-pipeline
readiness path across the declared routes, including legacy and 7.xx profiles.

The Advanced Debug catalog also expands with **Firmware Spoof, NP Fake
Sign-in, NanoDNS, Chukei DNS, zFTPd/zHTTP and Garlic Save Manager**.
Availability remains component- and firmware-dependent. These are additions
relative to the published tag, including dashboard/installer integrations
developed before the final 0.9.3 stabilization work.

### Engine choices and firmware routing

- Legacy firmware gains an engine selector and continuation to utility,
  daemon, Dashboard and Toolbox services after engine/Automount readiness.
  PS5HEN is available on the declared legacy entries; HyperCore selection is
  enabled only on the exact 3.xx/4.xx entries and remains unavailable below 3.00.
- On the exact 5.xx profiles, Bypervisor and HyperCore are separate choices.
  The dedicated Bypervisor path skips HyperCore, A53/PPR and BackPork, then
  continues to Automount and Dashboard services. HyperCore follows the
  compatibility startup path directly.
- Profiles **9.40 and 12.70** have dedicated startup routing and payload inputs,
  without borrowing neighboring firmware profiles.
- Profiles **13.40/13.42** now use an explicitly limited Kstuff-lite,
  Automount-lite and Dashboard/Toolbox route, replacing the previous
  utility-only mode. Backport/fakelib, emulator replacement and automatic
  AMPR features remain disabled in this lite path.
- An additional **experimental 13.60 Linux-loader input** is kept separate
  from the existing common clean-boot loader. This does not establish hardware
  certification for the experimental profile.

### Automount and languages

Automount is updated to **ShadowMountPlus 1.7 beta9**, with standard and lite
variants, stronger checks for conflicting live owners, and explicit
firmware/engine readiness before startup. Existing configuration and explicit
LAN binding are preserved; the default API address remains loopback.

Toolbox localization already existed in 0.9.2. This release extends
system-language handling to engine selectors and additional startup,
readiness, handoff and legacy-path notifications. The version update
preserves the existing Toolbox translations and customized project credit.
Rendering was checked for **31 system-language values using 28 catalogs**
and their aliases/fallbacks.

### Product branding

The welcome notification, Toolbox/About and HyperDeck now report **v0.9.3**.
Console Information displays **HyperHEN v0.9.3** instead of the inherited
OnionHEN label and retains the firmware information.

## Toolbox startup

The Toolbox now waits for WebKit's content process to exit, then requires a
continuous two-second quiet period before injection. A fresh process check
runs immediately before attachment. This shared startup guard applies to all
67 declared firmware profiles, from 1.00 through 13.60.

ShellUI replacement events invalidate stale readiness and are handled
separately from actual standby/resume. This improves recovery after launching
payloads from Advanced Debug or HyperDeck.

## Updated firmware description

The [current exact-profile table](README.md#exact-firmware-profiles) documents
all 67 entries: 25 legacy, four 5.xx selector entries, 25 A53/PPR entries,
the dedicated 9.40 route, six HyperCore routes without PPR, four hypervisor
routes and two Kstuff-lite entries. Legacy HyperCore selection remains limited
to the exact 3.xx/4.xx entries; it is unavailable below 3.00. The total of
67 declared profiles already existed in 0.9.2; this release updates their
routing and continuation behavior.

## Before testing

1. Perform a full reboot to replace the previous ShellUI module.
2. Launch HyperHEN 0.9.3.
3. Close WebKit manually if it is open, then allow the Toolbox to finish loading.

The R29 startup correction underlying this release was confirmed working on
the reported 9.05 tester. Local checks cover the 0.9.3 version metadata,
notification rendering for 31 system-language values, startup gates,
and preserved embedded components. This does not claim hardware testing of
every firmware profile.

The 0.9.3 version and branding update preserves the Kstuff, Automount beta9
and native HyperDeck inputs used by the tested R29 build. Five existing
profiles remain limited for on-demand ShellCore hooks:
**1.05, 1.10, 1.11, 1.13 and 4.02**. Their limited status is
unchanged by 0.9.3.

The existing kernel-log/FTP/debug-loader switches, HyperDPI installer,
plugin/module support, cheats, performance display, Remote Play pairing and
payload auto-start remain available; these are not newly introduced by 0.9.3.

If reporting a problem, include the exact firmware, the ELF version and a
klog from the same boot.
