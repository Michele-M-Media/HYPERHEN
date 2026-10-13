# HyperHEN vendor lock: HyperDPI

- Upstream source: `OnionBuddies/onionHEN-dpiv2-plugin`
- Upstream revision: `4e80389a87dd709f5f7ba316cbaa23d05a3cafc3`
- User-supplied reference ELF SHA-256:
  `b5ed9abec10bfd6fa33b8dd263c98ac7e55211f540da86a9c8719fb445db6776`
- License: GNU GPL-3.0; the upstream license and notices remain in this tree.
- Upstream plugin SDK revision:
  `0cf4346536705fb271ba0eb2226d97ebcb23559c`

HyperHEN builds the component from the pinned source, embeds it in the
bootstrapper, and launches it from memory only after the daemon plugin service
is ready. The external `DPIV00001.elf` is not copied or embedded.

The local overlay renames the component to HyperDPI, uses descriptor ID
`HYPD00001`, moves all state to HyperHEN paths, rebuilds the WebUI with the
HyperHEN visual identity, and points the compatibility client at
`/system_tmp/hyperhen/ipc/plugin_service`. Network listeners default to off;
the transfer/install behavior and port configuration remain source-equivalent
to the supplied reference build.

The research-only RNPS dump patch included in the supplied archive is not part
of the runtime.
