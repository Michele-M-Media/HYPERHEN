# HyperDeck — unified HyperHEN web console

> Integration and verification history. For the current 0.9.3 feature set,
> exact firmware profiles and release-compatible build, see the
> [README](../README.md), [release notes](../RELEASE_NOTES.md) and
> [build instructions](../BUILD_RELEASE.md).

## Current revision: 10 (source / host-verified, PS5 untested)

The root page is a single HyperDeck toolbox with five dedicated entries:
**Installer PKG Background**, **PS5 Game Compressor**, **PS5 Web File Manager**,
**CheatRunner**, and **PS5Upload**. HyperDeck remains the only
Media shortcut; the other four projects' Media-title installers and title
metadata are not invoked.

Revision 10 embeds the complete Game Compressor, Web File Manager, CheatRunner,
and PS5Upload 7.1.5 source engines. Selecting a
card starts that original native service and navigates the same browser to its
native loopback port. No API proxy, path translation, HTML rewrite, restyle or
replacement backend is used. Installer PKG Background keeps HyperDeck's existing
backend. PS5Upload is provided through HyperDeck rather than Debug Advanced; all other
Debug entries, firmware routes, and the startup pipeline are unchanged.

The Web File Manager, CheatRunner, LiveShell, and PS5Upload license/README
notices are retained under
`third_party/hyperdeck-tools/`. The supplied Game Compressor archive contains
an attribution README but no standalone license file; redistribution permission
must be confirmed with that project's author before a public binary release.

The four enabled engines are compiled from their vendored source and embedded in
`hyperdeck-websrv.elf`. They start only when selected: Game Compressor uses
port 5910, Web File Manager starts at 8888 and retains its original fallback
port scan, CheatRunner uses its original configured port (9999 by default),
and PS5Upload uses 9180 for its console interface.
Their original frontend and backend files remain together inside each engine.
The only build-time substitution is the separate Media-title registration hook;
HyperDeck remains the sole Media icon. CheatRunner's supplied snapshot contains
no standalone tile PKG, so its original tile-autoinstall path remains inactive.

LiveShell 1.4 remains in the vendored source tree for research, but is not
compiled, embedded, exposed in HyperDeck, or loaded into ShellUI. Hardware
testing showed that linking its renderer into the initial ShellUI module caused
`CE-108262-9`; keeping it quarantined restores the pre-integration boot path.

The reference is PIZZA HEN v2.00 public commit
`21948a05c49c4239059af4eb697f9a577348a899`, specifically
`launchHardwarePassPkg()` and `upstreamPkgArgs()`. The newly uploaded ZIP could
not be transferred (32 MiB tool limit); equivalence to that ZIP is not asserted.

The path is `HTML -> GET /hbldr?pipe=1&daemon=1 -> PKGInstall -> system
installer`. Only the fixed `/data/HyperHEN/bin/hyperhen-pkg-install.elf` helper
and an unchanged, inventoried local package are accepted. The helper retains
the supplied websrv 0.34 license and call structure, with a corrected local
ABI declaration checked at build time and terminal request-result reporting.
This does not mean the new helper binary is byte-identical to Pizza's prebuilt
ELF. HyperHEN adapts the
pipe/daemon transport to its existing ELF loader; it does not import Pizza's
core, replace HyperDPI, or introduce a second AppInstUtil implementation.

The helper is refreshed atomically at its managed destination when the embedded
copy changes. A request is shown as accepted only after the helper reports that
the system accepted it; this is still not system installation completion.
No fabricated percent, completion notification, retry, persistent queue,
firmware certification or resume-mode guarantee is supplied. Check the console's
Downloads / Uploads queue for actual installation status. Closing this page does
not deliberately kill the dispatched helper. Reboot/Rest Mode behavior requires
hardware testing.

Capacity is read from actual mount records: `/user`, `/mnt/ext0..1` (labelled
M.2 / extended storage, not positively identified hardware), `/mnt/usb0..7`.
Missing mounts are shown as not detected. Package scanning uses the same USB,
EXT and USB-alias root conventions as Pizza, with bounded recursion/catalogue
size; symlinks and changed files cannot be submitted through the HTTP route.

`HYPERHEN_HYPERDECK_MEDIA_LAUNCHER` now defaults to **ON**. After the local
service answers its readiness check, the existing HyperDeck Media shortcut is
prepared and its registration is requested. Media visibility can complete
asynchronously after that request. It opens the HyperDeck home; Installer PKG
Background is available at `/installer.html`. The local URL remains
`http://127.0.0.1:8088/`; a build must not pin
`HYPERHEN_STABLE_HYPERDECK_ELF` to the old r3 server if it is intended to embed
this new UI. Startup/core firmware selection and Automount are unchanged by
this revision. Source changes are not present in previously distributed ELFs.

Validation: `scripts/test_hyperdeck_http.sh` exercises the production router,
catalogue and UI with **host-only doubles** for mounts/ELF execution;
`scripts/test_hyperdeck_bootstrap.sh` checks both Media-shortcut and disabled
startup branches. The screenshots are explicitly host-test previews, not PS5
captures.

## Historical r3 Media implementation (not the current default)

The first native Settings/UI3 experiment was removed after the reported console
crash. All tracked ShellUI/Toolbox source files are restored to their pre-HyperDeck
state. Hyperdeck is now a separate process and local HTTP app, not another XML page.

## Start and open

Load the new experimental HyperHEN ELF after a clean console restart. After the
existing firmware pipeline succeeds, the bootstrapper directly spawns its embedded
`hyperdeck-websrv.elf` with the existing local ELF loader. It captures the actual
PID and redirects the child's early stdout/stderr into `HyperDeck.log`. The parent
then waits up to twenty seconds for that same PID's listener marker and a real
`/api/health` response identifying HyperDeck protocol 1.

Only after HTTP is verified does the parent prepare the platform services and
register the Media title. This follows the reference PizzaHEN separation of
websrv startup and parent-owned shortcut installation; HyperDeck keeps its own
UI, title, port and controls. Core startup and Toolbox injection precede this
stage. The local loader's spawn call retains its upstream blocking semantics;
the twenty-second bound applies to the subsequent HTTP readiness check.

On routes that include the classic Toolbox, this optional post-pipeline stage
waits externally for the live SceShellUI readiness marker before spawning the
web server. It does not change, delay internally, or replace the published
daemon/Toolbox injection logic. Legacy and kstuff-lite routes have no Toolbox
capability, so their completed Automount gate is the pipeline boundary.

The parent stages only `/user/app/HYPR00001/sce_sys/{param.json,icon0.png}` and
asks AppInstUtil to register that exact title. The application category follows
the proven Media shortcut category (`65536`), with display title
**Hyperdeck**, the original HyperHEN logo and URI `http://127.0.0.1:8088/`.
Open **Media → Hyperdeck**. No browser URL or external ELF is normally required.

An ownership marker in that dedicated title directory permits updates. A separate
registration marker is written only after AppInstUtil accepts the title and the
system's `/user/appmeta/HYPR00001/param.json` appears. If the request is accepted
but those system metadata are not visible yet, a private pending marker prevents
another registration attempt in the same startup window; a later invocation can
recover a stale pending state.
The installer uses the
SYSTEM context for both file staging and the registration call, restoring the
previous context on every return path. It tries the native module export first,
then the existing NID lookup. An unrecognized existing app or symlink is not overwritten.

There is no automatic app launch or foreground interruption. Existing websrv on
8080 and its FAKE00000 launcher are not modified, stopped or replaced.
Registration is idempotent and has no immediate retry. Network and user services
are prepared in the parent before registration; AppInstUtil loads dynamically at
this optional stage, not as a new startup dependency of the core or server. If
metadata staging or the exact registration API is unavailable, the HTTP process
remains available and publishes readiness; Media registration is deferred to a
later invocation. An invocation of this optional stage reuses an existing owned
server and checks registration again. A full HEN reload remains subject to the
existing conflict guard; use a clean boot for console retries. No AppInstallAll,
manual app.db writes, package repacking or guessed firmware offsets are used.

## Included and boundaries

The original 1340×740 card geometry, colors, typography sizes and three themes
are retained in `source/hyperdeck/include/hyperdeck_layout.hpp`.
The server generates real frames; the installed app never loads demo fixtures.

- **Overview**: card dashboard and best-effort live CPU/SoC temperature, fan and IP.
- **Reactor**: exact existing firmware route, completion markers for core and
  hypervisor, live PID markers for mount/daemon/Toolbox. Failure wins over ready;
  missing evidence is not interpreted as success.
- **Payloads**: inventoried local HyperHEN payload and USB folders, start/stop
  through existing path identities and utility/daemon IPC. No HTTP filesystem
  path or PID is accepted from the browser. It is not a recursive filesystem browser.
- **Plugins**: lists and starts/stops existing plugin IDs through daemon IPC.
  No delete, upload, arbitrary execution or plugin installation endpoint.
- **Quick controls**: existing klog 3232, FTP 2121 and debug ELF loader 9025.
  Requests set an explicit desired state, are serialized and require real replies.
- **Appearance**: HyperGlass, Reactor, Blackout, reduced motion. Browser local
  storage persists preferences; storage failure keeps them session-local.
- **Linux**: exact existing Linux firmware allowlist and explicit confirmation
  before writing the same next-clean-boot marker. It never loads Linux immediately.
- **Cheats, overlay configuration, full Toolbox**: remain in the original
  Settings Toolbox. These web cards explain where to open them; they do not
  pretend to apply unsupported actions. The overlay indicator is the saved
  preference, not proof that a game HUD is currently drawn.

D-pad / arrows navigate, Cross / Enter selects, L1/R1 or Q/E change sections,
Circle / Esc goes back. A web Gamepad API edge detector and keyboard fallback
replace the removed ShellUI input integration. Confirmation defaults to Cancel.
The PS button returns to the console home screen.

## Firmware policy

Existing HEN, Byepervisor, HyperCore, backport and Automount selection is not
changed. The optional HTTP process starts only after a successful exact route,
including legacy and 13.40/13.42, without adding daemon/Toolbox injection to
those restricted routes. The 13.40/13.42 core remains kstuff-lite plus Automount,
with no backport stage. Their web service controls that require util/daemon
remain unavailable rather than starting those services implicitly.
LiveShell is unavailable on every route while its renderer remains quarantined.

This is source-level integration, not an all-firmware compatibility certification.
Media registration, PS5 embedded-browser input, rendering, service actions,
repeated cold boot and Rest Mode must be tested on real hardware. If the
registration export is absent on a firmware, the app icon is not guaranteed.

## Isolation and recovery

The HyperDeck control service binds only to IPv4 loopback, port 8088. It is not
exposed to the LAN. The five original engines retain their upstream listener
behavior and ports.
Host allowlisting, per-process random CSRF tokens, same-origin POST controls,
no CORS and a restrictive Content Security Policy protect control endpoints.
The generic websrv filesystem/SMB/ELF upload/launch endpoints are not built.
Another listener is never killed to claim the HyperDeck control port. Shutdown
stops only native engine PIDs spawned and owned by this HyperDeck process before
the existing runtime services.

Create `/data/HyperHEN/no_hyperdeck` before loading HyperHEN to disable HTTP
startup. The icon may remain installed but cannot connect while the service is
off. Removing that marker re-enables startup on the next load.
Logs: `/data/HyperHEN/HyperDeck.log` plus the bootstrapper's HyperHEN log.
Revision 3 creates the log from the parent before spawning websrv. Even a failure
before the child's first initialization can therefore leave a parent stage record
and loader/child output. Spawn PID, process exit, HTTP verification and Media
registration results also appear in `HyperHEN.log`. Startup toasts distinguish
successful Media installation from an unmet Toolbox gate, failed spawn, early
process exit, HTTP failure and failed registration.
The diagnostic `hyperdeck_listener` and `hyperdeck_failed` markers are separate
from the final `hyperdeck_http` readiness marker. A setup failure is never
announced as an installed app. These checks do not prove the icon is visible in
the currently rendered Media screen; that still requires a console test.
For a complete return to the previous behavior, use the separately retained
Toolbox-restored ELF and reboot. The native-preview ELF should not be reused.

## Dashboard navigation

The HyperHEN dashboard entry opens the Toolbox. HyperDeck remains a separate
Media-tab entry for the integrated tool dashboard. Current startup and handoff
behavior is described in the release README.

## Build and verification

The first release-component recovery build did not complete its boot pipeline:
its extraction stopped HyperCore at a nested ELF signature, leaving 1,488,520
bytes instead of the release payload's complete 1,802,224 bytes. The missing
313,704 bytes include file-backed segment data and the section table. Comparing
hashes against that already truncated slice failed to detect the defect.

`scripts/verify_payload_elf.py` now validates every embedded ELF input's segment
and section boundaries before bootstrap linking. Its `extract-release` command
checks the release SHA-256 and packed bootstrap size/SHA-1, reads complete ELF
extents, and verifies each adjacent embedded size field. Nested ELF signatures
are skipped as part of their enclosing payload. Extraction reads files without
rewriting them. The exact public v0.9.2 binary remains the recovery baseline;
compiler version differences alone do not establish the cause of the earlier
post-Automount crash.

The server, UI, package-install helper and four native tool engines are
embedded in the HyperHEN ELF. The release-compatible 0.9.3 recipe rebuilds the
server/UI wrapper while retaining the verified native tool inputs; see the
current build instructions for that boundary.
The supplied websrv 0.34 asset registry is retained with its original GPLv3+
notices. The Media installer is adapted from John Törnblom's upstream sys.c.
The pinned libmicrohttpd 1.0.1 source archive and licenses are vendored;
its build stays in the build tree and uses the upstream-recommended O1.

- `bash scripts/preview_hyperdeck.sh`: exact firmware/state/input/layout checks
  and historical design-reference fixtures.
- `bash scripts/test_hyperdeck_http.sh`: production router, actual HTTP and
  browser tests with a visibly test-only host backend. No PS5 commands run.
- Existing HyperHEN host suite: checks the retained firmware and Toolbox logic.
- `bash scripts/test_hyperdeck_bootstrap.sh`: executes the optional launch stage
  against timeout, duplicate, stale-PID, loader-failure and disable-marker cases.
- `bash scripts/test_hyperdeck_launcher.sh`: executes the real installer with
  path-confined host IO and API doubles, including collision and partial-write cases.
- `bash scripts/test_hyperdeck_runtime.sh`: checks parent-created logs, redirected
  child output, SIGCHLD/credential cleanup and real socket health responses,
  including wrong services, failed status codes and split/truncated replies.
- Full PS5 SDK build: builds the five original native engines without their
  separate Media tiles, embeds them in `hyperdeck-websrv.elf`, then embeds that
  service in HyperHEN.

The host tests exercise Host/origin/token gates, authenticated native-engine
dispatch, original-port routing, explicit service actions,
unsupported routes, duplicate listener handling, themes, modal libraries,
escaped display names and desktop/mobile bounds. They do not establish actual
PS5 Media registration or console stability. Screenshots with HOST TEST are
browser tests, not PS5 captures.

Upstream: https://github.com/ps5-payload-dev/websrv
