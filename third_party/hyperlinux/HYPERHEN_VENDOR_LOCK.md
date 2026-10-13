# HyperLinux vendor snapshot

HyperHEN vendors the source supplied as `pLinux5-NG-master (1).zip` and builds
it locally as the embedded `hyperlinux.elf` boot engine.

- pLinux5-NG archive SHA-256:
  `316e25ecd25d021ba0318768ca623c2d3891a78c73c4cd7ecd038c259f1ebb64`
- ps5-linux-loader 2.5 archive SHA-256:
  `610fa2e1da7e131b4365c2a6e06961b79dc208da89ad6102f706777386911ecb`
- License: AGPL-3.0 for pLinux5-NG; retained GPL-3.0 components and notices
  from ps5-linux-loader.

Local integration changes are intentionally narrow:

- HyperLinux branding for its startup notification and built artifact.
- Exact 13.20 dispatch instead of an unverified 8.xx-13.xx range.
- Signed error checks for the bzImage and initrd loader paths.
- HyperHEN's clean-boot handoff and Debug-menu entry live outside this folder.
- `upstream-logo.webp` is the logo supplied with ps5-linux-loader 2.5;
  `upstream-logo.png` is its lossless build-tool-compatible conversion. The
  artwork is used for the HyperHEN Linux menu icon.

The supported exact profiles remain 3.00, 3.10, 3.20, 3.21, 4.00, 4.02,
4.03, 4.50, 4.51, 5.00, 5.02, 5.10, 5.50, 6.00, 6.02, 6.50, 7.00, 7.01,
7.20, 7.40, 7.60, 7.61, and the experimental 13.20 path.
