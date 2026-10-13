# HyperHEN visual system

HyperHEN uses one visual language across HomeUI, notifications, and the XML
Toolbox: a graphite core, cyan energy edge, ultraviolet transition, and a
restrained magenta accent. The central HyperCore emblem is deliberately more
detailed than menu glyphs; operational icons use one octagonal frame and a
high-contrast white silhouette for legibility at 64 px.

## Compatibility contract

- Every Toolbox surface is native `system_settings` XML and remains usable
  from the legacy 2.x profile through the current 13.x profile.
- `hyperhen_sicon.png` is an APNG. Its first frame is a complete standalone
  PNG icon, so decoders without APNG playback show the static emblem.
- Animation is decorative. Navigation, focus, actions, and values never rely
  on frame playback.
- Menu PNGs are checked-in release assets. Builds stage them without requiring
  Inkscape, librsvg, or ImageMagick.

## Asset workflow

- `assets/logo.png` and `source/bootstrapper/assets/logo.png` are the master
  emblem exports.
- `scripts/render_hyperhen_brand.sh` renders the 128 px APNG from the master.
- SVG glyph masters live in `source/bootstrapper/assets/icons` and the matching
  64 px release PNGs live one directory above.
- `scripts/theme_hyperhen_icons.py` applies the common frame to newly imported
  glyphs; render the SVGs to their matching PNG names before packaging.
