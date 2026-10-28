# CheatRunner — Changelog

## v0.18

- **Added: 16 languages** with flags and a language picker; Arabic and Persian read right to left.
- **Added: controller shortcut and a "★ CheatRunner" game-menu entry** that open a game's trainer (off by default).
- **Added: download the official PS-Game-Patch release from Sources.**
- **Added: cheat repositories download as one archive** with a live progress panel; unchanged files are skipped.
- **Added: XML patches placed in /data/cheatrunner/patches are recognized**, in any letter case.
- **Added: title ID, PS5/PS4 badge, version, time played and last played on every game card.**
- **Added: Visual effects setting (Full / Reduced);** Reduced is the default in the PS5 browser.
- **Added: command palette (Ctrl/Cmd+K), collapsible menu and a QR code to open the dashboard on a phone.**
- **Added: update notice, and Discord, X, Bluesky and Support buttons in the top bar.**
- **Added: Sources links to hencheats.vercel.app and PS-Game-Patch.**
- **Added: a tip in the Patches tab and the README about patches that only work at game start.**
- **Added: a health summary in System and a Troubleshooting section in Settings.**
- **Added: step-by-step debug logs for the system-UI hooks;** the log level defaults to debug and the log file keeps every session.
- **Added: a short build id in the log header and version line,** so each session shows which ELF was running.
- **Changed: `build-cheatrunner.sh` shows one progress line, prints the end of the log when the build fails, and has `--clean`, `--sqlite`, `--no-sqlite`, `-j` and `--verbose`.**
- **Added: the log shows the original and new bytes of every patch line.**
- **Changed: cheats turn on and off with a single click;** address modes, the address cache and the verify/force options are gone.
- **Changed: a version or address mismatch no longer locks a cheat's switch.**
- **Changed: new dashboard** with a left menu, a full-page trainer (Cheats / Patches / Files) and a shorter Settings page.
- **Changed: Midnight is the default theme, and the dashboard uses the system font.**
- **Changed: "HEN Cheats Collection" is marked Recommended in Sources.**
- **Changed: long lists load in batches and covers are small cached thumbnails,** so the PS5 browser stays smooth.
- **Changed: autoload waits 1 second after every cheat and patch it turns on.**
- **Changed: UI polish** — motion, loading skeletons, dialogs, filters and phone layouts.
- **Changed: the system-UI hook waits 10 s after the shell's libraries load** and follows new shell processes, including after rest mode.
- **Changed: the system-UI hooks are written from inside the shell in one step** and skipped when they can't be placed safely.
- **Changed: CheatRunner stops hooking after two shell restarts that follow a failed injection** and shows a notice.
- **Changed: new dashboard look with a readiness strip on the trainer page, a slimmer top bar and clear empty and error states.**
- **Changed: the dashboard keeps its effects in desktop and modern browsers, and the PS5 browser uses the lighter Reduced preset until you pick Full in Settings.**
- **Changed: Discord, X, Bluesky and a labelled Support button are back in the top bar, and cheat and patch authors are shown prominently.**
- **Added: a download in progress shows in the top bar on every page.**
- **Added: new names to the supporters list.**
- **Changed: the Support text is larger and easier to read, and the heart floats again** (not in the Reduced preset).
- **Changed: CheatRunner.elf is about 3.5 MB smaller** (smaller logo, lossless tile images, unused code removed at link time, bundled libraries built for size).
- **Removed: unused code,** including the old online cheat search and its `/api/sources/jobs`, `/api/cheats/sources` and `/api/cheats/remote/*` endpoints.
- **Added: CheatRunner Patches repository in Sources,** with a patch source picker.
- **Changed: when a game has patches from both repositories, only the CheatRunner Patches ones are listed;** they are saved in their own `cheatrunner` subfolders, so PS-Game-Patch never overwrites them.
- **Fixed: a cheat file that failed to load was shown as missing, and a failed state read left the switches off without saying why.**
- **Changed: CheatRunner never restarts or kills the system UI anymore;** hooks left by an earlier session are reused.
- **Changed: hooking the system UI no longer alters the shell's own permissions,** keeps its extra rights only while working, and the hook stays marked as installed even if the handshake fails.
- **Changed: "Report an issue" opens the GitHub issue tracker, and builds use the Makefile only.**
- **Fixed: the version and name of homebrew games mounted from a folder were not detected,** so their cheats showed "version undetected".
- **Fixed: cheats with a long Mastercode name (for example The Witcher 3) showed their sub-cheats as conflicting;** turning one on switched the Mastercode off and on again.
- **Fixed: cheats and patches with large byte strings could not be enabled.**
- **Fixed: patch XML files were not found** (names with spaces or brackets, new files, sub-folders).
- **Fixed: XML patches that jump into a code cave could crash the game.**
- **Fixed: a patch with a pattern line that finds no match is no longer undone entirely;** the lines that match are applied.
- **Fixed: the controller shortcut and the ★ menu on firmware 4.xx,** where the system UI has more modules than the hook could list.
- **Fixed: downloaded cheat files with a hash or process name after the version (for example PPSA31246_01.200.000_ef4d3664.mc4) lost their version and matched any game version.**
- **Fixed: cheats and patches on firmware 13.60.**
- **Fixed: the Patches tab reloaded and jumped when a patch was applied or restored.**
- **Fixed: game covers cut off or missing in the PS5 browser.**
- **Fixed: laggy scrolling, blue lines after switching tabs and a cut-off Autoload pill in the PS5 browser.**
- **Fixed: the dashboard layout on phones, especially in Arabic and Persian,** and the unstyled page when the stylesheet failed to load.
- **Fixed: time played and last played now match the PS5 home screen.**
- **Fixed: the controller shortcut gave up for good after a few rest-mode wakes.**
- **Fixed: system-UI crashes and restarts caused by injection** (a stalled injection, unsupported instructions, restarting CheatRunner).
- **Fixed: the ★ menu entry was sometimes missing after a game launch.**
- **Fixed: the controller shortcut restarting the system UI** after the shell opened its own web processes.
- **Fixed: a new CheatRunner could not replace the running one, and the browser could keep an old dashboard.**
- **Fixed: downloads stop with a clear message when files cannot be saved.**
- **Fixed: the D-pad works right away in the PS5 browser.**
- **Removed: the Create-button shortcut, "Find cheats online", "Copy cheats folder", the profile editor, thermals and fan panel, and the notification bell.**

<details>
<summary><b>v0.17.2</b></summary>

- **Fixed: writes on FW 13.60.**

<details>
<summary><b>v0.17.1</b></summary>

- **Added: support for 13.60 firmware** — rebuilt against the latest ps5-payload-sdk.

<details>
<summary><b>v0.17</b></summary>

- **Fixed: the trainer/cheat menu, Settings page, and Support/donate popup could silently fail to appear on some PS5 firmwares** (reported on 4.03) — the "Cheat menu loaded" toast would show, but the menu itself never appeared, same for Settings and Support. Root cause, confirmed against that firmware's actual WebKit source: every full-screen overlay in the dashboard positioned itself with the CSS `inset` shorthand, which is gated behind WebKit's `CSSLogicalEnabled` runtime flag — off by default in this build. With `inset` silently dropped, the overlay never got a `top`/`right`/`bottom`/`left`, so it had no size or position to render at, regardless of `display`/`visibility`/`opacity`. (Two earlier attempts in this same release — switching `display:none` to `visibility`/`opacity`, then adding a `transition` — treated it as a compositing/paint timing issue and didn't address this.) Replaced `inset` with explicit `top`/`right`/`bottom`/`left` everywhere it was used for positioning (the three overlays, plus several decorative background/glow/shimmer effects that had the same silent failure but went unnoticed since they're cosmetic). Confirmed fixed on the reporting user's PS5.
- **Fixed: the Settings page's ON/OFF toggle switches were too cramped for the text**, and the green "ON" fill sat slightly off-center — a CSS math bug on every toggle switch in the app, not just Settings.
- **Second PS5 browser performance pass, focused on scroll smoothness.** Game tiles, buttons, and switches no longer permanently promote to their own GPU layer — a real scroll-jank source with dozens of tiles on screen, now disabled on PS5 only. Game icons lazy-load, decode off the main thread, and get resized to display size server-side on first cache instead of shipping the full 512×512 source. Search is debounced instead of rebuilding the grid on every keystroke, the health panel stops re-rendering every 10 seconds while collapsed, and a few more always-on animations are now disabled on PS5.
- **The trainer/cheat menu now goes fullscreen on the PS5 browser** instead of a centered dialog with wasted space on a TV. PC/desktop keeps the centered layout.
- **The hotkey now opens straight to the trainer for the game currently running**, instead of the dashboard's main page. From the XMB, with no game running, it still opens the main page.
- **Added a controller hotkey.** Hold two buttons together to open the CheatRunner dashboard in the system browser — L2 + R3 by default, configurable from Settings → Hotkey. Re-hooks automatically within ~20 seconds after rest mode or a SceShellUI restart.
- **Fixed: the hotkey could stop responding to a changed combo, or stop working entirely, if SceShellUI still had a hook resident from an earlier session.** Now detected — SceShellUI restarts once automatically to clear it.
- **Fixed: launching CheatRunner while an instance was already running didn't kill the old one**, requiring a manual shutdown from the dashboard first.
- **Fixed: the hotkey could crash SceShellUI (XMB) instead of hooking in**, on some firmwares. The injector freed a scratch memory region the instant setup finished — but a newly-created thread reaching that point only means it was *created*, not done bootstrapping onto its own stack. It could still be touching that memory, and freeing it corrupted unrelated state and crashed SceShellUI. Confirmed fixed on PS5.
- **Hardened the hotkey's Mono module lookup** to use a fixed-size buffer instead of a heap allocation, avoiding a possible interaction with SceShellUI's own memory allocator.
- **Fixed: three of the six names in the Support modal's "Special thanks" list were animating in sync** instead of independently — a missing per-name delay left the 1st, 5th, and 6th names sharing the same phase.
- **Added an "Enable hotkey" switch in Settings → Hotkey, off by default.** On some firmwares (seen on 4.xx) the hook can still crash SceShellUI despite the fix above — the rest of CheatRunner works fully with it off; turn it on once you've confirmed it's stable on your firmware.
- **Fixed a garbled "…" character** ("Search settings…", the health panel's "Loading…") showing as mojibake on the PS5 browser — replaced with plain periods.

</details>

<details>
<summary><b>v0.16</b></summary>

 **Removed the "Disable All Patches" button.** It disabled patches for every game on the console at once, which was confusing and easy to trigger by accident. Patches now enable/disable per-entry only, the same as cheats.
- **Fixed: "CheatRunner is not responding" errors reported by several users.** Root cause: HTTP keep-alive (added late in v0.15) held connections open far longer than before, and the PS5 browser doesn't reuse them the way keep-alive expects — this could exhaust the concurrent-connection limit under normal polling. Reverted back to closing each connection after one request.
- **Added a setting to disable the home-screen tile auto-install** (Settings → System). On by default, matching the existing behavior.
- **Fixed: "Download Cheats" searches could fail with "Request timed out"** even when the search was still completing fine in the background — a single flaky status check no longer cancels the whole search.
- **Raised the HTTP server's connection backlog** (8 → 32) to reduce refused connections during bursts of simultaneous requests.
- **Reorganized the Repositories tab layout** for clarity, and fixed its progress bar/result colors not respecting the active theme.
- **The Repositories tab now shows which files failed to verify after a bulk download**, plus when the last download finished.
- **Added a "Reset to Auto" button for the fan-on threshold** (Thermals & Fan panel) — clears a pinned threshold and lets the console's automatic fan curve take back over.
- **Fixed a DNS resolution security weakness.** Query IDs were predictable, and the socket wasn't restricted to the server it queried, both making responses easier to spoof.
- **Dashboard now serves gzip-compressed HTML/CSS/JS** when the browser supports it — faster page loads, especially on the PS5 browser.
- **Added cheat autoload profiles.** Save your currently-enabled cheats for a game as a profile, and CheatRunner automatically re-applies them the next time that game launches, with an on-screen notification when it fires.
- **Replaced "Copy Cheat Debug" and "Copy Diagnostic Bundle" with a single "Copy Logs" button** in the trainer modal, matching the one already on the Logs panel.
- **Added patch autoload profiles.** Save your currently-applied patches for a game as a profile, and CheatRunner automatically re-applies them the next time that game launches, same as cheat autoload profiles.
- **Added an "Autoload" badge on game tiles** for titles with a saved cheat and/or patch autoload profile, so you can tell at a glance without opening the trainer.
- **Patch autoload now always finishes before cheat autoload starts** on game launch (previously both fired at the same time), so cheats that depend on a patched code path never race against the patch itself.

</details>

<details>
<summary><b>v0.15</b></summary>

- **Added `mask_jump32` patch support.** Previously parsed but always skipped as unsupported, silently no-op'ing several real patches (multiple "60 FPS"/"Resolution Patch" entries).
- **Added a "Switch to Max Compatibility" prompt** when a cheat fails to enable due to an unverifiable address. Doesn't change any defaults — only offers the existing preset at the moment it would help.
- **Added trainer author credits** to the cheat menu ("Cheats By") for JSON, SHN, and MC4 files.
- **Fixed:** SHN/MC4 game names with an apostrophe (e.g. "Assassin's Creed") were truncated in the cheat menu.
- **Fixed:** cheats depending on a "Master Code" mod could resolve to the wrong address and fail silently, including a case where a dependent's own name confused master-code detection. Also fixes the dashboard's MISMATCH badge showing incorrectly for these cheats.
- **Fixed:** cheat/patch writes silently failed on firmware newer than 12.00.
- **Fixed:** some valid cheats sharing a code-cave block with other mods were wrongly blocked as "wrong address."
- **Fixed:** cheat entries with a dropped leading hex zero (e.g. `"1"` instead of `"01"`) failed to parse and silently did nothing.
- **Added a network watchdog** that recovers faster after sleep/wake or Wi-Fi reconnects, and notifies you when the console's IP changes.
- **Added an OLED theme** — true black background and panels, and noticeably less glow/decorative lines than the default Dark theme.
- **Session logs are now saved to `/data/cheatrunner/logs.txt`**, not just kept in memory — makes it possible to check what happened after a crash or a restart wiped the in-app log view.
- **Fixed: "Shutdown Payload" could silently hang forever** instead of closing CheatRunner, if a cheat apply was stuck at the exact moment shutdown was requested. Shutdown now always completes within a couple seconds either way.
- **Enlarged the header logo** and made its glow match the active theme's color instead of always being red.
- **Fixed:** the Cheats/Repositories/Patches tabs in the trainer modal could render as squashed, oversized capsules on the PS5 browser.
- Attempted fix for a PS5-browser-only bug where the trainer modal's Patches/Repositories lists couldn't be scrolled down to reach lower content — improved but not fully confirmed resolved on PS5 yet.
- **Fixed:** a Master Code cheat could show a false "Conflicts with active mod" against its own dependent, blocking it from being enabled, when the dependent's own name also mentioned "Master Code."
- **Fixed:** cheats that require a Master Code showed a confusing "VERSION MISMATCH" (and couldn't be toggled) before the Master Code was enabled — now shows a clear "requires Master Code first" state instead.
- **Fixed:** a Master Code's own button showed a false "PARTIAL PATCH" once its dependent cheats were enabled and writing into its shared cave, even though everything was working correctly.
- **Fixed:** the Settings popup could fail to open/render on the PS5 browser the same way the trainer modal did — it was missing a fix already applied elsewhere.
- **Game icons are now cached by the browser** instead of being re-downloaded every time the dashboard loads — faster repeat loads, especially on the PS5 browser.
- **Settings theme dropdown now shows proper names** ("Dark", "Crimson", "Midnight", "OLED") instead of raw internal values.

</details>

<details>
<summary><b>v0.14</b></summary>

- **CheatRunner's home-screen tile now installs into Media Players instead of the Games library**, plus a follow-up `param.json` audit (badge type, locale key, DRM type) found by comparing PS5's own app database against a known Media-tab app.
- **Fixed:** dashboard updates (new buttons, PS5 perf fixes) weren't showing up on PS5s that had already loaded the page — the AppCache manifest wasn't being bumped every release, and no JS handler existed to actually swap in an updated cache when one was detected. Both fixed.
- **PS5 browser performance pass.** Removed Google Fonts (blocking external font load), added HTTP caching for static assets, replaced `backdrop-filter` blur with solid backgrounds, hid decorative background layers, converted several `box-shadow`/`clip-path` animations to cheap `opacity`/`transform`-only ones, disabled tile-stagger and a couple of scan-line animations during cheat toggle/launch, and stopped tile-selection from rebuilding the entire game grid on every click.
- **Migrated the memory read/write engine from ptrace to mdbg.** ptrace required stopping the whole game process for every read/write, and two threads attaching at once could corrupt an in-progress patch — this was the root cause of games freezing mid-cheat-apply. Confirmed fixed on PS5. Added a CR3-walk fallback for firmware 8.20+, where Sony's native mdbg write call silently stops working.
- **Went fully mdbg-only in the write path** — `kernel_mprotect` and the old ptrace-based code-cave remap fallback were removed entirely from writes.
- **Full security/reliability audit (~22k lines).** Removed dead HTTP auth code that was never wired up, closed a shutdown race that could interrupt an in-flight write, and fixed several smaller bugs: an undefined-behavior shift in patch hex parsing, a duplicate constant, a signed-overflow bug in address math, an unguarded config read, and cross-module false positives in conflict detection.
- **Fixed two real kernel-panic/console-freeze incidents**, both reproduced on PS5 and traced to `kernel_mprotect` itself (not page-protection state) corrupting kernel memory on certain addresses. `kernel_mprotect` was removed from both the write path and the execute-only-memory read fallback; the patch mask-pattern scanner no longer scans past the first unreadable memory chunk.
- **Fixed a rapid-toggling freeze**, where a stalled PS5 notification call could lock up the entire cheat-apply subsystem because it ran while holding the apply lock. Also reduced the HTTP server's per-request memory allocation from a flat 4 MB to a small buffer that grows only when needed.
- Added Maffioh to the Support modal's "Special thanks" list.
- Bumped to **0.14**.

</details>

<details>
<summary><b>v0.13</b></summary>

- **Fixed:** cheats/patches targeting execute-only game code (`.text`) always showed as unreadable/mismatched, even when correctly applied. Reads now temporarily unlock the page, read, and restore its original protection. (This approach was later replaced in v0.14 for stability reasons.)
- **Added a crash log.** SIGSEGV/SIGBUS are now caught and appended to `/data/cheatrunner/crash.log` instead of the daemon dying with no trace.
- **Added a Thermals & Fan panel** — live CPU/SoC/NVMe temperatures, a manual fan-on threshold slider (persisted across launches/reboots), and a °C/°F toggle.
- **Added a User Profile panel** — rename the active user and change their profile picture directly from the dashboard.
- **CheatRunner now installs its own home-screen tile automatically** on first boot.
- **Fixed:** "Memory Patch — Not Ready" was shown even on a fully working jailbreak — a status field was missing from the API response the dashboard actually reads.
- **Fixed two off-by-one bugs** in process-list scanning that could make CheatRunner miss its own process or the running game's pid on certain kernel versions.
- **Fixed:** payload redeploys could fail to reclaim the HTTP port from a stuck previous instance; the port is now released immediately instead of waiting on the old process.
- Removed two long-dead, riskier write functions in favor of the one path everything already used; upgraded the address-guessing heuristic from checking 1 instruction byte to 2, cutting false positives significantly.
- Live download progress for remote cheat sources; a warning toast when results come from a stale cached index.
- Collapsible, reorderable sidebar panels; fonts (Space Grotesk/Inter/JetBrains Mono) that were configured but never actually loading now load correctly.
- The running game is now pinned to the top of the game grid; tiles show playtime or last-played date; profile picture upload gained a crop/pan/zoom step before saving.
- Supporter credits added to the Support modal.

</details>

<details>
<summary><b>v0.12</b></summary>

- **Fixed `bind() failed: 48` dashboard disconnects.** CheatRunner tried to kill a previous instance of itself *before* escalating its own privileges, so the kill was silently rejected and the old instance kept holding the port forever. The kill now happens after escalation, and failed binds retry with a clear, bounded message instead of spamming forever.
- **Ptrace stability audit** — fixed a credential-elevation race across three threads that could leave CheatRunner's process permanently over- or under-privileged, a competing attach between the game monitor and an in-progress cheat apply, and an address-cache buffer size bug that silently dropped the entire learned-address cache.
- **Fixed "CheatRunner is busy" / "not responding" under game load** (mainly PS4 BC titles) — dashboard polling could spawn enough hung ptrace-attach threads to exhaust the server's thread pool. Added liveness checks, skips polling while an apply is in progress, and shortened timeouts.
- **Fixed a crash from concurrent thumbnail requests** — SQLite is compiled non-thread-safe in this build, and loading many game icons at once could corrupt its internal state and crash the whole daemon. Now serialized behind a lock.
- **Fixed the HTTP server not reconnecting after PS5 sleep/wake** and a related crash-detection gap where a dead game process could still be reported as running.
- **MC4/SHN address-resolution and cave-writing bugs, several fixed the same week:** a poisoned address cache could brick every mod in a cheat file instead of self-healing; code-cave pages were stripped of write permission after a cheat was applied, freezing the game the next time it wrote to that page; multiple "Master Code" groups in one file could resolve against the wrong group; a scan fallback could compute an address missing its module base entirely, landing on arbitrary memory; and the `allow_legacy_*` "I accept the risk" settings didn't fully apply to every guard that should have respected them.
- **Added an x86-instruction heuristic** to auto-pick the correct address for SHN/MC4 cheats with no verified baseline — reads a few candidate bytes and picks whichever looks like real code, instead of guessing.
- **Added an address-learning cache** so resolved addresses don't need to be re-probed on every apply.
- **Enabled code-cave fallback and master-code auto-fixup by default**, and added cave-overwrite/JMP-redirect support so switching between mutually-exclusive cheats (e.g. Speed Normal/3x/5x) no longer requires a game restart.
- **Patch engine hardening:** the per-patch line cap was silently truncating large patches (some 60 FPS patches have 100+ lines) — raised from 64 to 640 lines. Fixed a kernel panic and a separate SIGILL crash in the PS4 BC patch-write and XML-parsing paths, added full backup/rollback (all-or-nothing) with hard failure on verify mismatch, and blocked applying patches with unsupported or zero usable lines instead of silently no-op'ing them.
- **Patch engine now supports multiple XML files per title** (including a new `elf-arsenal` search path), with stable IDs so applied-state survives rescans.
- **Dashboard overhaul:** animated tab switching, refreshed icons/buttons/focus states, loading skeletons, a Favorites/Recents system with first-run onboarding, and a full Settings redesign — one-click Safe/Max Compatibility/Debug presets, plain-language labels, risk warnings on dangerous toggles, search, and reset-to-defaults.
- Smaller fixes: shutdown now uses SIGKILL instead of a graceful exit that could hang; crash suspects and disabled-cheat state now persist across restarts; a concurrent-write race could corrupt cached files (icons, address cache) sharing the same temp-file name; game version detection now checks more paths and no longer probes a kernel path that could panic older firmware; PlayStation Store lookup as a last-resort title-name source.

</details>

<details>
<summary><b>v0.11</b></summary>

- **Reduced log noise significantly** — several very chatty per-write and per-poll log lines were downgraded to debug level or throttled to fire only on change, cutting routine log volume by roughly 5–10x.
- **Split the dashboard's HTML/CSS/JS into separate files** served at their own routes, shrinking the main HTML shell from ~3000 lines to under 200.
- **Fixed PS4 BC cheat writes failing silently** — added a fallback write path for pages where the standard permission query isn't supported.
- **Version detection improvements** — now checks more SFO/param paths (including update/patch and external-drive locations) and warns once per game instead of spamming when a version genuinely can't be found.
- **Added a launch-status generation counter** so a stale, slow-to-complete launch attempt can no longer overwrite the status of a newer one; added a watchdog that auto-recovers a launch stuck in "busy" past a timeout.
- **Centralized cheat-file version matching** into one shared comparison function, fixing several inconsistent-match edge cases (leading zeros, trailing `.0` segments, unparseable strings silently "matching").
- **Added manual cheat-file selection** — when auto-selection doesn't pick the right version, you can now force a specific candidate file, with a visible mismatch warning if you do.
- **Compatibility:** added `/data/etaHEN/cheats` and `/data/elf-arsenal/cheats` as cheat search paths.
- **Cheat engine stability fixes** — a fast double-attach race, a stuck "no cheat file" error after a game restart, and a false "bytes mismatch" that blocked disabling MC4/SHN cheats with no explicit expected-bytes baseline.
- **Added remote cheat source downloads** — cheats can now be fetched automatically from configured repositories on game launch.
- **Added a SQLite-backed game list**, title-name resolution, playtime/activity tracking, and a redesigned sliding ON/OFF toggle switch with proper mixed/error/applying states.

</details>

<details>
<summary><b>v0.1</b></summary>

- Initial release
- ptrace-based memory write engine (`pt_copyin` / `pt_copyout`)
- Cheat format support: JSON, SHN/XML, MC4 (AES-256-CBC encrypted XML)
- Basic cheat enable / disable via HTTP API
- Game process detection and monitoring

</details>