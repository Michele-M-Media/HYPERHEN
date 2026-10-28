# HyperHEN plugin runtime candidate

This candidate adapts the user-supplied TacHen source archive's native plugin
workflow to HyperHEN. It is not a claim of byte-identical implementation or
universal plugin/firmware compatibility. Console validation is outstanding.
Provenance and GPL notices are preserved under
`third_party/tachen-plugin-runtime/`.

## User workflow

Keep the current HyperHEN Tools interface. L3+R3 and TacHen's menu are not
imported. The current profile must already run the daemon and Toolbox: the
existing 13.40/13.42 lite-only routes remain core + Automount only.

Standalone etaHEN-compatible `.plugin` files go in `/data/HyperHEN/plugins/`.
Start one manually from its existing Plugins page, preferably once inside the
intended game. These plugins are independent native processes, not PRX modules.
Saved `.auto_start` markers are ignored for this format. A missing dynamic
HyperHEN SDK settings page is not an error for a third-party standalone plugin.
There is no generic safe stop/reload: reboot the console to unload resident
standalone code. Existing native HyperHEN descriptor-bearing `.elf` plugin
features are retained separately.

PRX/SPRX files retain the existing `/data/HyperHEN/sprx/catalog.ini` catalog.
Open the matching game, enter gameplay, then choose the existing module's
**Load in current game** toggle. It now actually dispatches a load, through the
existing asynchronous progress panel; it no longer merely saves a future-start
flag. If no version is registered, that explicit manual operation pins the
entry to the running exact title and mounted version. Optional `game_version`
can also be supplied in the manifest. A version mismatch rejects the load;
there is no silent retargeting. Dependencies must match the same target.

The toggle reflects confirmed initialization in the current game, not a saved
enable flag. Close the game to unload its resident modules. Do not try to use a
generic toggle-off as patch rollback. The candidate does not scan arbitrary
folders, import TacHen's catalog, or guess a third-party module's intended game.

## Native behavior

* Standalone containers retain their 29-byte header parsing and exact 9-character
  process identity. The private loader has a separate `hplg:` protocol for these
  images, using TacHen's bounded ELF mapper, dynamic/section relocation validation
  and SDK v0.43 payload argument ABI. Legacy internal payloads keep their route.
  Old private loaders reject `hplg:` instead of silently using the wrong ABI.
* The plugin-only spawn uses bounded stopped-child waits, checks the breakpoint
  stop and retains a shared child stack if ownership cannot be established.
  SIGCHLD disposition is changed/restored only in the single-threaded private
  loader, not in the daemon or console-global pipeline.
* A durable standalone intent precedes dispatch. Boot identity, file fingerprint,
  process name and process start time prevent a lost reply or reused PID from
  becoming a second dispatch or a false recovery. A confirmed process is evidence
  of dispatch, not proof of every plugin feature.
* PRX/SPRX modules use read-only executable nullfs-backed storage in the uniquely
  verified game's sandbox. Source uploads are never edited. Plaintext development
  PS5 module SDK declarations are adapted only in a disposable verified copy,
  under the same applicability checks as TacHen. Retail/PS4 containers are not
  rewritten. There are no new DRM or firmware patches in this integration.
* Exact mounted title/version metadata and process start identity are rechecked.
  The metadata digest is checked again before starting a worker. The process root
  and jail directory must remain stable throughout the operation.
* A game-owned pthread calls the normal module loader and captures **both** the
  handle and initializer result. Paired `plugin_load`/`plugin_unload` exports select
  one additional `plugin_load(0, NULL)` activation; ordinary module_start-only
  modules are not initialized twice. Neither module presence nor a positive PID
  is substituted for that result.
* Per-game receipts prevent retries after uncertain/failed initialization and
  deduplicate the same staged image under aliases. At most eight distinct image
  attempts are admitted per game process. Temporary worker code/data are retained
  until game exit so a still-returning worker cannot execute unmapped memory.
  A slow initializer can be observed as completed later without dispatching it
  again; its stored metadata digest must still match the current game.
* Unlike TacHen's input-callback scheduler, HyperHEN creates its worker through
  its existing ptrace transport. TacHen's controller hooks, renderer, JavaScript
  coordinator and phone interface are not imported. Hardware acceptance must
  specifically check games whose thread creation/initialization timing differs.

## Verification and build isolation

Host tests cover manual dispatch, dependency failure, version pinning/mismatch,
resident unload refusal, standalone autostart/restart suppression, UI controls,
metadata parsing, dedicated protocol replies and malformed/old-loader rejection.
`scripts/test_plugin_runtime.sh` additionally executes the **PS5-built worker
binary** with host callbacks, covering ordinary initialization, paired activation
and failure cases; it checks synthetic ELF and development SELF fixtures.
`scripts/test_plugin_transport.sh` executes the production PRX transport against
mocked console primitives and real local receipts, including late completion,
alias deduplication, uncertain/failed attempts, the eight-image cap, PID reuse and
credential restoration. These mocks do not validate real ptrace or mounts.

The new daemon, its embedded Toolbox, and the private loader are source-built
when `HYPERHEN_SOURCE_PLUGIN_RUNTIME=ON`. Stable release util and all established
core/mount/hypervisor/backport-related payloads remain pinned through
`HYPERHEN_RELEASE_COMPONENT_DIR`. The optional HyperDeck r3 server and its startup
order are unchanged (`HYPERHEN_STABLE_HYPERDECK_ELF` pins the r3 server in this
candidate). Passing these tests does not establish console module-load,
nullfs, ptrace, suspend/resume or third-party plugin compatibility.
