# Game Compressor 1.0.4

Game Compressor 1.0.4 adds a multilingual interface and improves compatibility
with firmware 4.51, APR-EMU folder workflows, and ShadowMountPlus launch
layouts.

Compared against `v1.0.3`.

## Key Changes

- Added German, French, Arabic, Italian, Spanish, and Simplified Chinese
  translations, with automatic system-language selection and a saved manual
  language selector.
- Added right-to-left layout support for Arabic and localized Game Compressor
  launcher titles for every supported language.
- Improved firmware 4.51 browser compatibility and added clearer web-server
  startup diagnostics.
- Improved relaunch handling when a previous Game Compressor instance leaves
  port 5910 open but no longer answers the local HTTP handoff.
- Added ShadowMountPlus executable discovery under `/data/ps5_autoloader` in
  addition to Payload Manager locations.

## Fixes

- Fixed `Build AMPR Index` for directly detected game folders so enqueue and
  worker resolution use the same APR-EMU probe result.
- Fixed APR-EMU module file permissions for affected deployment and loading
  paths.
- Fixed the compression wizard so `Delete after verified` keeps its own
  free-space and deletion explanation when switching source-preservation
  options.

See `CHANGELOG.md` for the full detailed change list.
