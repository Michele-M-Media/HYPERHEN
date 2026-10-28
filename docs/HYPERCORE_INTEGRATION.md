# HyperCore integration

HyperHEN produces one user-facing payload, `build/bin/HyperHEN.elf`. The
loader, utility process, daemon, ShellUI payload, HyperCore, HyperHEN Automount,
HyperDPI, and firmware-routed HEN/Hypervisor stages are intermediate build products
embedded into that final file. They are not sidecar files that must be copied
to the console.

The initial injection transport is still supplied by the exploit environment:
HyperHEN does not contain a kernel exploit and expects a first-hop `elfldr` on
port 9021. Once started, it launches its embedded private loader on port 9020.

## HyperCore source

HyperCore is built from `third_party/kstuff-a53` commit
`0a69ea31759f048d24b38bfaa23dbec1fd065916`. It combines:

- the kstuff-lite source base;
- exact-profile A53/PPR installation and rest-mode verification;
- the native BackPork process monitor and `fakelib` mount lifecycle;
- the loader's USB/title image scan and mount support.

The upstream control flow and compatibility-sensitive names are kept intact.
HyperHEN adds build orchestration plus a branding-only overlay for user-facing
notifications and diagnostic stage labels. `scripts/build_hypercore.sh`
compiles it from source and CMake embeds the result. The old kstuff-lite 1.11
submodule, GitHub release download, cache stamp, and
`/data/HyperHEN/kstuff.elf` runtime override are not part of this tree.

The compatibility release package keeps firmware-specific builds in distinct
slots. The common `hypercore` image serves the existing routes,
`hypercore_940` remains the byte-identical trusted 9.40 image, and the new
`hypercore_1270` image contains the 12.70 prerequisite-map addition. This
prevents a 12.70 source change from replacing the 9.40 or common-profile core
in the final release ELF. A normal all-source development build may point
multiple slots at the same freshly built source artifact.

## Automount source

HyperHEN Automount is built from the supplied ShadowMountPlus 1.7-beta5fix1
snapshot under `third_party/shadowmountplus`. CMake produces
`hyperhen_automount.elf` from source and embeds it in the bootstrapper. The
service publishes a process-owned readiness marker only after its startup scan
has completed and publishes a failure marker if initialization aborts.

The automount stage is route-independent: the legacy branch starts it after the
legacy HEN completion marker, while newer branches start it after HyperCore is
verified. Its SceShellCore bridge uses only the 62 exact profiles supplied by
the upstream table. Unsupported bridge offsets are not guessed; those optional
hooks fail closed without changing the base mount engine. An offset-free
SysCore `NOTE_CHILD` fallback is armed only while the verified bridge is
inactive. It resolves the title and sandbox during the pre-exec window, then
prepares package redirects and fakelib layers through the same Automount
owners. This closes the legacy backport gap on 1.05, 1.10, 1.11, 1.13, and
4.02 without adding synthetic ShellCore profiles.

## Hypervisor source

`third_party/byepervisor-reborn` is built by
`scripts/build_hypervisor.sh` and embedded in the bootstrapper. Firmware 12.70
uses the isolated `hypervisor_1270` image and then `hypercore_1270`; firmware
13.00, 13.20, and 13.60 retain the common hypervisor and HyperCore images.
HyperHEN launches the selected stage before HyperCore, waits for its Rest
Mode/resume completion marker, then starts kstuff and BackPork. Those versions
have no exact A53/PPR profile in the
supplied core, so the Hypervisor completion marker is the gate that permits
the kstuff route. The initial resume-hook cookie is not accepted as readiness:
the supervisor payload publishes a distinct completion cookie only after every
discovered VMCB reports nested paging disabled. Other firmwares follow the
centralized capability matrix:
exact 6.00–11.40 profiles normally use A53/PPR, while 9.40 first requires its
dedicated Byepervisor post-resume gate and then uses a dedicated HyperCore
image whose PPR installer is explicitly disabled;
exact 11.60, 12.00, 12.02, 12.20, 12.40, and 12.60 profiles
use the reduced no-PPR route, and 13.40/13.42 use the dedicated kstuff-lite
route without Byepervisor, A53/PPR, or BackPork.

The final layout contains 21 embedded components. Relative to the trusted
prior layout, its only new slots are `hypervisor_1270` and
`hypercore_1270`; `hypercore_940` and every common component retain their
prior bytes.

The supplied ps5-hen snapshot is built as the internal HEN route for its
25 exact profiles from 1.00 through 4.51. The supplied Byepervisor 5.xx source
is built for 5.00, 5.02, 5.10, and 5.50; after its completion marker HyperCore
continues without rerunning PPR. These routes are mutually exclusive.

## Startup order

```text
first-hop elfldr :9021
  -> HyperHEN.elf
  -> embedded bootstrapper
  -> embedded private loader :9020
  -> firmware branch
       legacy: FW 1.00–4.51 exact profile -> embedded legacy HEN -> completion marker
       modern: embedded util.elf
         -> FW 5.00/5.02/5.10/5.50: embedded Byepervisor 5.xx -> completion marker
         -> FW 12.70: embedded hypervisor_1270 -> Rest Mode -> resume marker; select hypercore_1270
         -> FW 13.00/13.20/13.60: common embedded Hypervisor -> Rest Mode -> resume marker; select common HyperCore
         -> FW 11.60/12.00/12.02/12.20/12.40/12.60: explicit no-PPR marker
         -> selected embedded HyperCore
       lite: FW 13.40/13.42 -> explicit kstuff-lite marker -> embedded HyperCore
       1. verify/install A53/PPR, validate a Hypervisor marker, or validate the exact no-PPR route marker
       2. start kstuff
       3. apply ShellCore patches transactionally, verify each write, and publish success only after completion
       4. require both the HyperCore completion marker and an independent userland mprotect post-check
       5. patch app.db and start the rest-mode monitor where supported
       6. keep native BackPork/USB mount providers paused under automount ownership
  -> embedded HyperHEN Automount -> readiness marker
  -> modern branch: embedded daemon.elf -> Toolbox
    -> embedded HyperDPI client -> dynamic Toolbox page (listeners off by default)
```

FW 13.40/13.42 start only the exact-profile kstuff-lite core and HyperHEN
Automount. They do not start util, Byepervisor, A53/PPR, BackPork/backport,
daemon/Toolbox, HyperDPI, plugins, or payload autostart.

## Exclusivity

HyperHEN assigns one owner to each capability. Loading another provider
concurrently is unsupported:

| Capability | HyperCore provider | Conflicting duplicate |
|---|---|---|
| Kernel component | kstuff-a53 | kstuff-lite 1.11 or a runtime `kstuff.elf` override |
| A53/PPR lifecycle | integrated PPR sources | standalone ppr-patch payload |
| Hypervisor lifecycle (12.70) | embedded `hypervisor_1270` stage | standalone Byepervisor instance |
| Hypervisor lifecycle (13.00/13.20/13.60) | common embedded Byepervisor Reborn stage | standalone Byepervisor instance |
| Hypervisor lifecycle (5.00/5.02/5.10/5.50) | embedded Byepervisor 5.xx stage | standalone 5.xx instance |
| Legacy HEN lifecycle (exact 1.00–4.51 profiles) | embedded ps5-hen route | a second HEN route on the same boot |
| Early fakelib mount | HyperHEN Automount; integrated BackPork is sentinel-paused | standalone BackPork |
| USB/title mount monitor | HyperHEN Automount; HyperCore scanner is sentinel-paused | external ShadowMountPlus or another automounter |

The ShadowMountPlus source is now the embedded HyperHEN Automount provider, not
an external plugin. The bootstrapper creates `/data/.kstuff_noautomount` before
HyperCore starts. Both the HyperCore scanner and its native BackPork thread
observe that ownership and remain paused; they can resume only if automount
ownership is released after a failed startup. Neither Byepervisor
implementation is folded into HyperCore; each remains a separate
firmware-routed stage.

The 13.40/13.42 route is stricter: its marker prevents the A53/PPR resume
monitor and BackPork thread from being created at all. Only the kstuff-lite
core remains active beneath the common Automount owner.

## Supplied-source disposition

| Supplied archive | HyperHEN disposition |
|---|---|
| onionHEN 0.9.0 beta 2 | Runtime and Toolbox base; visibly rebranded while retaining the `onion::*` compatibility ABI and upstream notices |
| kstuff-a53 1.4 snapshot | Vendored, source-built, embedded, and launched as HyperCore |
| kstuff-lite 1.11 | Exact 13.40/13.42 tables validated against the supplied source; the matching vendored base is selected by an isolated source-built route, with no sidecar or runtime override |
| ppr-patch | Superseded by the A53/PPR implementation already integrated in kstuff-a53 |
| ShadowMountPlus | Vendored, source-built, embedded, and launched as the exclusive HyperHEN Automount stage |
| onionHEN DPI v2 plugin | Vendored at commit `4e80389a87dd709f5f7ba316cbaa23d05a3cafc3`, rebranded as HyperDPI, source-built, embedded, and launched from memory after the daemon |
| Byepervisor Reborn | Base `ea04f7132b837087f552c57261cb800481a3a910` is vendored for 13.00/13.20/13.60; the exact 12.70 profile from archive commit `0c353514d90d51d9f1233586b33620355833ae93` is built into an isolated stage |
| Byepervisor 5.xx | Vendored, source-built, embedded, and used for FW 5.00/5.02/5.10/5.50 |
| ps5-hen | Vendored, source-built, embedded, and used as the HEN route before common automount for its exact 1.00–4.51 profiles |
| OpenGoldHEN | All-in-one product/UX reference only; no second runtime or incompatible ABI is layered over OnionHEN |

This is deliberate capability consolidation, not concatenation of mutually
exclusive payload entry points. It preserves the supplied core behavior and
keeps one owner for each kernel, PPR, and mount lifecycle.

## Firmware status

The supplied combined source reports runtime validation on firmware 5.00 for
the A53/PPR route. The locked base Byepervisor source declares exact offsets
for 13.00, 13.20, and 13.60. The later archive at
`0c353514d90d51d9f1233586b33620355833ae93` supplies the exact 12.70 table,
reported upstream on regular and Pro hardware; HyperHEN's isolated 12.70 route
still requires end-to-end console validation. The newly supplied legacy engine declares 25 exact
profiles from 1.00 through 4.51, and the 5.xx source declares 5.00, 5.02, 5.10,
and 5.50. ShadowMountPlus supplies 62 exact optional SceShellCore profiles from
1.00 through 13.60; its integration and the firmware routes still require
end-to-end console validation. Legacy revisions without an exact ShellCore
profile use the offset-free pre-exec fallback for backport preparation; this
does not turn them into exact ShellCore-hook profiles.
HyperCore requires an exact A53/PPR profile, the matching successful
Hypervisor readiness marker (including the dedicated no-PPR 9.40 route), or the explicit exact no-PPR compatibility marker
for 11.60, 12.00, 12.02, 12.20, 12.40, and 12.60. Every HyperCore route must publish its completion marker and
pass the independent runtime `mprotect` post-check before dependent components
start. ShellCore writes are range-checked, read back, and rolled back if a
later static-patch or package-hook stage fails. The no-PPR route disables the
integrated plaintext/PPR hooks. FW 13.40 and 13.42 instead use their supplied
exact kstuff-lite tables and an isolated route marker; no Hypervisor, PPR
lifecycle, or BackPork stage is started.

No claim of universal firmware support should be made until each firmware,
boot path, title set, and rest-mode cycle has reproducible console logs.
