# HyperHEN vendor lock: plugin client runtime

- Upstream source: `OnionBuddies/onionHEN-plugin-sdk`
- Upstream revision: `0cf4346536705fb271ba0eb2226d97ebcb23559c`
- License: GNU GPL-3.0; the upstream license remains in this tree.

The stable `onion_*` C symbols, descriptor layout, and wire constants are
retained as a compatibility ABI. The local overlay changes the visible SDK
identity and the production socket path to HyperHEN's runtime namespace. It is
built only as the client runtime linked into the embedded HyperDPI component.
