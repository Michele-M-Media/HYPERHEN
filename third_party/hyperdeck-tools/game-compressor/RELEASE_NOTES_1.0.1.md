# Game Compressor 1.0.1

Game Compressor 1.0.1 focuses on APR game support on internal SSD, image
creation, and cleaner storage workflows.

Compared against `v1.0.0`.

## Key Changes

- Added `Make Image` for APR Emu workflows. Game Compressor can create direct
  `.exfat` or `.ffpfs` images from folder games so APR Emu titles can run from
  internal SSD.
- `.exfat` is the recommended and faster image type for this workflow.
- For image creation, Game Compressor automatically builds or refreshes
  `ampr_emu.index` when needed and automatically applies the ShadowMountPlus
  read-only image setting for the created image.
- Added `Set Read Only` for existing image entries. Game Compressor writes the
  ShadowMountPlus read-only image hint and requests a rescan for the selected
  image.
- Improved ShadowMountPlus source discovery by respecting configured `scanpath`,
  `scan_depth`, `recursive_scan`, and manual-list entries.
- Improved uncompress/decompression destination handling for image and folder
  outputs.
- Simplified compression choices by removing the old Fast/miniz profile path and
  the temporary raw-only user flow.
- Consolidated move/copy actions into target pickers for internal and external
  storage.
- Bug fixes: clearer output-exists errors and better UI failure notices.

See `CHANGELOG.md` for the full detailed change list.
