# Third-Party Notices

## DPI pkgserver

The source under `third_party/pkgserver/` is migrated from OnionHEN's DPI v2
integration and is distributed with this GPLv3 project. It incorporates work
and techniques credited in the source to:

- LightningMods and etaHEN
- cy33hc and ezRemote-DPI
- soniciso and elf-arsenal
- ps5upload
- kvnhrt

The plugin-specific changes provide a stoppable lifecycle, configurable API
and WebUI listeners, embedded WebUI delivery, localized notifications, and an
adapter boundary independent of OnionHEN's utility daemon.

## WebUI dependencies

The browser application under `webui/` uses Preact, Vite,
`@preact/preset-vite`, `vite-plugin-singlefile`, Lucide React, ESLint, and
their transitive npm dependencies. Their individual license notices are
recorded by `webui/package-lock.json` and remain governed by their respective
licenses.
