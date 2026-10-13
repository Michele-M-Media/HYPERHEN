# Contributing

Keep the change small. Put it in the process that already owns that job.
Do not invent a parallel path.

The map is [docs/arch.md](docs/arch.md). Update that file when you change
modules, IPC, runtime paths, or dependencies.

## Runtime

```
HyperHEN.elf (unpacker)
  → bootstrapper.elf
    → hyperhen_elfldr.elf :9020
      → util.elf → embedded HyperCore → daemon.elf
        → inject shellui.elf into SceShellUI
```

The first hop still needs an external `elfldr` on **9021**. After that,
user payloads and internal ELFs go through the private loader on **9020**.
Startup is serial on purpose: util, then HyperCore, then daemon. Do not start
those three in parallel.

## Where a change belongs

| If you are changing… | Put it here |
|---|---|
| First boot, remount, update block, ELF launch order | `source/bootstrapper/` |
| Toolbox inject, fan rewrite, crit IPC, app jailbreak | `source/daemon/` |
| Cheats, heavier I/O, util IPC | `source/util/` |
| Toolbox pages, OnPress, overlay, language list | `source/shellui/` |
| LZMA wrap of the bootstrapper | `source/unpacker/` |
| Shared config schema | `source/libonion_settings/` |
| Notifications, log, small FS helpers | `source/libonion_platform/` |
| Unix IPC client / server | `source/libonion_ipc/` |
| User-facing strings | `source/i18n/` |

Reuse `libonion_*`, `libNineS`, `libhijacker`, and `libNidResolver`.
Do not copy a helper into a page file because it is closer.

Settings persist through `onion::Settings` and the twin `config.ini` paths.
A Toolbox save is `settings_commit` → `BREW_RELOAD_SETTINGS` → `LoadSettings`.
That rereads config in daemon and util. It does not inject ShellUI again.

## Code

- Match the `snake_case` already in `source/`.
- Leave third-party file names as the upstream project spelled them.
- New Toolbox UI goes through `ps5ui::Page` in `toolbox_xml.cpp` and the
  existing OnPress tables. Do not add a second menu builder.
- New toasts use stable `notify.*` keys via `onion_notify`. Do not pass an
  English sentence as the key.

## Strings

Toolbox and notification text live in `source/i18n/*.json`. Add or change a
key in **every** locale file, and keep the `printf` placeholders the same.
See [source/i18n/README.md](source/i18n/README.md).

A new JSON file is compiled automatically. It is not selectable until you
also add the settings value, Toolbox list item, and `Lang` /
`ONION_NOTIFY_LANG_*` mappings.

## Build and tests

```bash
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh
make -C source/util/tests test
```

Host tests live in `source/util/tests/`. They cover settings, IPC, cheats,
Toolbox routing, and i18n. Run them even if you cannot flash a console.

Products go under `build/`. HyperCore's Makefiles may create intermediates in
`third_party/kstuff-a53/`; do not write generated products into `source/`.

## Pull requests

Use the Bug report or Feature request form for issues. Firmware, loader,
and logs matter more than a long write-up.

Contributions are under the same [GNU GPL v3](LICENSE) as this tree.
