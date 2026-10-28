<p align="center">
  <img src="CheatRunner.png" alt="CheatRunner" width="240"/>
</p>

<h1 align="center">CheatRunner</h1>

<p align="center">
  <b>A web-based cheat trainer for your PlayStation 5!</b>
</p>

<p align="center">
  <a href="https://github.com/notmaj0r/CheatRunner/releases/latest"><img src="https://img.shields.io/github/v/release/notmaj0r/CheatRunner?label=Release&logo=github&color=blue" alt="Latest release"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3"/></a>
  <a href="https://discord.gg/E4g6fEqp46"><img src="https://img.shields.io/badge/Discord-Join%20our%20Server-5865F2?logo=discord&logoColor=white" alt="Join our Discord Server"/></a>
  <a href="https://ko-fi.com/maj0r"><img src="https://img.shields.io/badge/Ko--fi-Support-FF5E5B?logo=kofi&logoColor=white" alt="Support on Ko-fi"/></a>
</p>

---

**CheatRunner** runs as a single ELF on an already-jailbroken PS5 and serves its complete interface over HTTP.

No desktop client is required. Send `CheatRunner.elf` to the console, open the PS5 IP address on port `9999` in a browser, and manage your games and cheats from a desktop, tablet, or smartphone.

CheatRunner brings the most common PS5 trainer workflows together in one interface:

- Browse installed games and apps, with covers, version, play time, and last played;
- Launch installed titles from a browser;
- Load local cheat files (`.mc4`, `.shn`, `.json`) and turn cheats on or off with one click;
- Apply PS-Game-Patch XML patches;
- Download cheat collections from community sources;
- Re-apply your cheats and patches automatically when a game starts;
- Open a game's trainer from a controller shortcut or the game's options menu;
- Inspect logs and diagnostics.

> **One payload. One dashboard. No companion application required.**

---

## ✨ Features

- **Games library** — PS5 / PS4 / Apps filters, covers, title ID, version, time played, last played, and one-click launch.
- **Favorites and Recent** — stored on the console, so every device that opens the dashboard sees the same lists.
- **Full-page trainer** — per-game **Cheats**, **Patches** and **Files** tabs, a readiness strip, and the cheat and patch authors shown up front.
- **Cheat formats** — `.mc4`, `.shn`, and `.json`; master-code and code-cave cheats of any size are supported.
- **One-click cheats** — click to turn on, click again to turn off, with post-write verification and rollback.
- **Version-aware selection** — exact version first, generic file as fallback, wrong version as a last resort with a visible warning.
- **PS-Game-Patch XML patches** — version-aware (`AppVer`), with mask-version patches applied regardless of game version.
- **Autoload profiles** — save the cheats and/or patches that are on; they are re-applied the next time the game launches, with an on-screen notification.
- **Crash-suspect detection** — mods that crashed the game are flagged and blocked from re-enabling automatically.
- **Controller shortcut and game menu** — hold two buttons (L2 + R3 by default) or pick **★ CheatRunner** in a game's options menu to open that game's trainer.
- **Cheat sources** — download a whole repository as one archive with a live progress panel; unchanged files are skipped.
- **16 Languages** — complete UI translation with RTL support for Arabic and Persian.
- **Themes and visual effects** — five themes, plus a **Full** and a **Reduced** effects preset; Reduced is the default in the PS5 browser.
- **Responsive interface** — designed for desktop, tablets, and smartphones.
- **Phone access** — a QR code opens CheatRunner on your phone.
- **Command palette** — quick keyboard navigation with `Ctrl/Cmd + K`.
- **Logs and diagnostics** — a health summary, a Copy Logs button, and a per-session log file.
- **Config hot-reload** — edits to `config.ini` over FTP/SSH are picked up automatically, no restart needed.
- **Update notice** — shows a message when a newer GitHub release exists.

---

## 🚀 Getting Started

### Requirements

CheatRunner is intended for a PS5 that already has a compatible jailbreak/homebrew environment running.

You will need:

- An already-jailbroken PlayStation 5;
- A compatible ELF/payload loader;
- A computer, phone, or tablet on the same local network as the console.

### Run CheatRunner

Send:

```text
CheatRunner.elf
```

to the PS5 using your preferred ELF loader.

Then open the dashboard from another device on the same network:

```text
http://<PS5-IP>:9999
```

Example:

```text
http://192.168.1.100:9999
```

CheatRunner serves the complete frontend directly from the ELF.

### Homebrew Launcher

CheatRunner also ships as a Homebrew Launcher package (`CheatRunner.zip`).

1. Extract the zip **directly into `/data/homebrew/`** on your PS5 via FTP.
2. Check that the result is:

   ```text
   /data/homebrew/CheatRunner/
   ├── CheatRunner.elf
   ├── homebrew.js
   └── sce_sys/
       └── icon0.png
   ```

3. Open the Homebrew Launcher on your PS5, launch **CheatRunner**, then open the dashboard in your browser.

---

## 🔄 Updating CheatRunner

**Before updating to a new version, please follow these steps:**

1. Go to `/data/cheatrunner` and delete **everything** except the `cheats` and `patches` folders.
   - Keep both folders — there is **no need** to delete them.
   - Do this **before** using the new version, **not after**.
2. Delete the old **CheatRunner PKG** (home-screen tile) from your PS5.
3. Clear your browser cache: **Settings → System → Browser → Clear Cache**.

Then send the new `CheatRunner.elf` as usual.

---

## 🎮 Games & Trainer

The **Games** page lists everything installed on the console, with the PS5 / PS4 / Apps filters, search, favorites, and recent titles.

Each game card shows the title ID, a PS5/PS4 badge, version, time played, and last played, plus **Play** and **Trainer** actions.

The trainer opens as a full page with three tabs:

- **Cheats** — switches for every mod in the loaded cheat file;
- **Patches** — the PS-Game-Patch entries that match the game and its version;
- **Files** — the cheat files found for this title, with a selector to force another candidate.

Cheat and patch authors are shown on the page and on each patch, so credit always stays visible.

---

## 🧰 Local Cheats

CheatRunner is local-first. Local cheat files remain the primary workflow.

### Search paths (scanned in order)

```text
/data/cheatrunner/cheats   ← primary path
/data/etaHEN/cheats        ← etaHEN cross-compatibility
/data/elf-arsenal/cheats   ← elf-arsenal cross-compatibility
```

Recommended sub-folders inside the primary path:

```text
/data/cheatrunner/cheats/mc4
/data/cheatrunner/cheats/shn
/data/cheatrunner/cheats/json
```

Example files:

```text
/data/cheatrunner/cheats/mc4/PPSA30803_01.200.000.mc4
/data/cheatrunner/cheats/mc4/PPSA30803.mc4
/data/cheatrunner/cheats/shn/CUSA00000.shn
/data/cheatrunner/cheats/json/CUSA00000.json
```

### Version-aware selection

CheatRunner matches cheat files by title ID and game version:

- **Exact match** (`CUSA00000_01.09.json`) — selected automatically when the running game reports that version.
- **Generic** (`CUSA00000.json`, no version in the filename) — used as fallback when no exact match exists.
- **Wrong version** — loaded as a last resort when no exact or generic file exists; the trainer shows a version-mismatch warning and a `WRONG VER` badge. The candidate selector lets you force-switch or download a better match.

---

## 🩹 Local Patches

CheatRunner supports **PS-Game-Patch** XML patches.

Patch search paths (scanned in this order):

```text
/data/cheatrunner/patches/xml_prospero   ← PS5-native patches
/data/cheatrunner/patches/xml            ← PS4 BC / general patches
/data/elf-arsenal/patches/xml            ← elf-arsenal cross-compatibility
/data/cheatrunner/patches                ← loose .xml files
```

Notes:

- Sub-folders (such as a downloaded repo) are scanned too, and `.xml` is matched in any letter case.
- Files need a `<TitleID>` block; ones without it are skipped and logged as `index_skip no_title_id`.
- New files are picked up automatically, or press Refresh in the dashboard.
- **CheatRunner Patches** are saved in `xml/cheatrunner` and `xml_prospero/cheatrunner`. If a game has patches there, only those are used and PS-Game-Patch files for the same game are ignored; games they don't cover keep using PS-Game-Patch.

### Patches that must be applied at game start

Some patches only take effect if they are written in the first seconds of the game, because the game reads the value once at boot (for example the RDR2 60 FPS patch). Applying them while the game is already running writes the bytes but changes nothing.

1. Open the game and apply the patch from the **Patches** tab.
2. Click **Save autoload**.
3. Close the game and open it again. CheatRunner applies the patch on its own as soon as the game starts.

---

## 🌐 Sources

The **Sources** page downloads community cheat and patch collections straight to the console:

- HEN Cheats Collection (recommended);
- PS5 Cheats;
- GoldHEN Cheats;
- HEN PPSA Cheats;
- CheatRunner Patches (community patches released by the maintainer);
- The official PS-Game-Patch release.

Repositories are fetched as a single archive, with live progress, speed, and ETA. A download in progress also shows in the top bar on every page.

By default, files that already exist are left untouched and only new files are added. Turn on **Also update files that changed** to refresh existing files with the repository's latest version; local-only files are never deleted. Keep it off if you edited a file by hand.

---

## 🔁 Autoload Profiles

Autoload profiles re-apply your cheats and/or patches every time you launch a game — no need to open the dashboard and toggle them on again.

**Important:** an autoload profile only saves the cheats/patches that are **currently ON/applied at the moment you save it**. It does not save the whole cheat or patch file.

### Cheats

1. Open a game's trainer and enable the cheats you want.
2. In the **Cheats** tab, click **Save Autoload Profile**.
3. Next time you launch that game, CheatRunner re-applies those cheats and shows an on-screen notification.
4. To stop, click **Clear Autoload Profile**.

### Patches

1. Open a game's trainer and apply the patches you want.
2. In the **Patches** tab, click **Save Autoload Profile**.
3. Next time you launch that game, CheatRunner re-applies those patches and shows an on-screen notification.
4. To stop, click **Clear Autoload Profile**.

Cheat and patch profiles are independent — you can save one, both, or neither for any game.

---

## 🕹️ Controller Shortcut & Game Menu

Off by default. Turn on **Controller shortcut and game menu** in Settings to:

- hold the chosen two buttons (L2 + R3 by default) while a game is running to open its trainer;
- get a **★ CheatRunner** entry in the game's options menu that opens that game's trainer.

This works by hooking SceShellUI, so it needs a few seconds after boot or rest mode to arm. CheatRunner never restarts the system UI. If the console misbehaves, turn the option off in Settings and restart CheatRunner.

---

## 🎨 Look & Feel

- **Themes** — five themes; Midnight (blue) is the default.
- **Visual effects** — **Full** keeps blur, glow, and motion; **Reduced** turns them off for the PS5 browser. Reduced is chosen automatically on the PS5 until you pick Full in Settings.
- **Phones and tablets** — the layout adapts down to small phone widths.

Tip: keep the dashboard open on a PC or phone while playing. The PS5 browser competes with the game for resources.

---

## 🌍 Languages

CheatRunner supports a fully localized interface in:

- 🇺🇸 **English**
- 🇸🇦 **Arabic**
- 🇨🇳 **Chinese Simplified**
- 🇫🇷 **French**
- 🇩🇪 **German**
- 🇮🇹 **Italian**
- 🇯🇵 **Japanese**
- 🇰🇷 **Korean**
- 🇮🇷 **Persian**
- 🇵🇱 **Polish**
- 🇧🇷 **Brazilian Portuguese**
- 🇷🇺 **Russian**
- 🇪🇸 **Spanish**
- 🇹🇭 **Thai**
- 🇹🇷 **Turkish**
- 🇺🇦 **Ukrainian**

English is the default language. Arabic and Persian use RTL layouts, while technical values such as paths, URLs, title IDs, versions, and logs stay in their natural LTR form.

---

## 🧪 Notes & Bug Reports

CheatRunner is still experimental. Some cheats may work correctly, fail to apply, mismatch the game version, crash the game, require a specific game update, or behave differently after relaunching the game.

When reporting bugs, include:

- CheatRunner logs (Copy Logs button, or `/data/cheatrunner/logs.txt`);
- the game title ID and version/update;
- the cheat file used;
- which cheat was enabled or disabled;
- what happened before the issue.

Each log session starts with a header line, and the log level is `debug` by default.

---

## ⚠️ Disclaimer

CheatRunner is **not a jailbreak**, **not an exploit**, and **not a tool for stock/retail consoles**.

It only works when your PS5 is already jailbroken and already has the required homebrew/payload environment running.

CheatRunner does **not** provide:

- jailbreaks or kernel exploits;
- game backups or game content;
- piracy features;
- PSN bypasses;
- online services.

It is a local homebrew tool for testing trainers on your own already-jailbroken console.

Runtime cheat memory writes can crash the game, break a session, corrupt your savegame, softlock the title, or behave differently depending on the game version/update.

**Always back up your save files before using any cheats.**

Use CheatRunner at your own risk.

---

## 🛠️ Building

CheatRunner is built with the **PS5 Payload SDK** and a plain `Makefile`. On Windows, build inside WSL.

You need the SDK, `make` and `python3` (the dashboard, i18n, and payload blobs are generated at build time), plus a host C compiler (used to pre-compress the dashboard).

```sh
make            # writes build-make/CheatRunner.elf
make clean
```

`./build-cheatrunner.sh` wraps `make` with a tool check, a single progress line and a build summary (size, SHA-256, time):

```sh
./build-cheatrunner.sh                # normal build
./build-cheatrunner.sh --clean -j 4   # wipe build-make/ first, 4 parallel jobs
./build-cheatrunner.sh --verbose      # full make output
./build-cheatrunner.sh --help         # all options (--sqlite, --no-sqlite, --skip-js-check)
```

Each log session starts with a short build id, so reports say which ELF was running.

The Makefile looks for the SDK in this order:

```text
1. /opt/ps5-payload-sdk
2. PS5_PAYLOAD_SDK environment variable
3. ./PS5-Projects/sdk-master
4. ./ps5-payload-sdk
```

SQLite: if `src/third_party/sqlite3.c` and `sqlite3.h` exist, bundled SQLite is enabled automatically. Use `make SQLITE=1` to force it on and `make SQLITE=0` to disable it.

Output:

```text
build-make/CheatRunner.elf
```

---

## 📁 Repository Layout

```text
.
├── build-make/     Build output
├── pkg-source/     PS5 home-screen tile sources
├── src/
│   ├── web/        Dashboard (HTML, CSS, vanilla JS, locales)
│   ├── payload/    Code injected into SceShellUI
│   └── third_party/ Bundled libraries
├── tools/          Build and validation scripts
├── Makefile
└── build-cheatrunner.sh
```

The dashboard is written as plain files in `src/web/`; `make` compresses and embeds it directly into `CheatRunner.elf`.

---

## ❤️ Support

CheatRunner is free and open-source.

If you find it useful, consider supporting further development.

<a href="https://ko-fi.com/maj0r"><img src="https://img.shields.io/badge/Ko--fi-Support-FF6433?style=for-the-badge&logo=kofi&logoColor=white" alt="Support CheatRunner on Ko-fi"/></a>
<a href="https://buymeacoffee.com/maj0r"><img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-Support-FFDD00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black" alt="Support CheatRunner on Buy Me a Coffee"/></a>

**PIX - Brazilian method**

**LivePix**: https://livepix.gg/notmaj0r

**Donate via Crypto**

- **BTC**  
  `bc1ppfcad9ytyzmmzq3nfatfcag7q38rqcpsvxfqusq0uzv4gh82pa4syeknlk`

- **BCH (Bitcoin Cash)**  
  `qz2h97c2pyexn3fwhveu33tn5xkd3qlfn5796zhtcq`

- **USDC (Solana)**  
  `BNpezXSQWuUR9HWGjzQUS7AeEBCwiY2ZGxArWNwYXKKV`

- **USDT (TON)**  
  `UQA18f4cVS0778fL_nIssdN1GIHRYR7dQ3UnNgjPwgYvIQi_`

---

## 🤝 Credits & References

CheatRunner exists thanks to the PS4/PS5 homebrew and research community.

- **ELF Arsenal & VoidShell** — for project ideas and implementations.
- **[Ps5-Payload-Dev](https://github.com/ps5-payload-dev)** — for the [SDK](https://github.com/ps5-payload-dev/sdk) CheatRunner is built with.
- **[etaHEN](https://github.com/etaHEN/etaHEN)** (LightningMods) and **[onionHEN](https://github.com/aydencharles/onionHEN)** (aydencharles) — the SceShellUI injection and hooking approach behind the controller shortcut and the ★ CheatRunner game-menu entry is adapted from their work.
- **miniz** (Rich Geldreich) and **[qrcode-generator](https://github.com/kazuhikoarase/qrcode-generator)** (Kazuhiko Arase) — bundled libraries used for unpacking cheat archives and the QR code.
- **TeeKay87** — for the [HEN-Cheats-Collection](https://github.com/TeeKay87/HEN-Cheats-Collection) project.
- **etaHEN** — for the [PS5_Cheats](https://github.com/etaHEN/PS5_Cheats) project.
- **GoldHEN** — for the [GoldHEN_Cheat_Repository](https://github.com/GoldHEN/GoldHEN_Cheat_Repository) project.
- **RDX-Sci01** — for the [HEN-PPSA-Cheats](https://github.com/RDX-Sci01/HEN-PPSA-Cheats) project.
- **illusionyy** — for the [PS-Game-Patch](https://github.com/illusionyy/ps-game-patch) and [ps-patch-system](https://github.com/illusionyy/ps-patch-system) projects.
- Everyone testing, reporting issues, sharing logs, and helping improve the project.

---

## 📜 License

CheatRunner is licensed under the **GNU General Public License v3.0**.

See the `LICENSE` file for the full license text.

```text
GNU General Public License v3.0
```

---

<p align="center">
  <b>CheatRunner</b><br/>
  One dashboard for your jailbroken PS5.
</p>
