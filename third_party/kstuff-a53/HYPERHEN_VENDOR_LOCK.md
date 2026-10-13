# HyperHEN vendor lock

The `kstuff-a53` source in this directory is the source snapshot supplied for
HyperHEN. Its Git archive comment identifies commit:

`0a69ea31759f048d24b38bfaa23dbec1fd065916`

The upstream snapshot referenced three Git submodules but did not include
their contents. HyperHEN vendors the exact Gitlink revisions so the core can
be built without downloading a precompiled ELF:

| Dependency | Upstream revision | Source archive SHA-256 |
|---|---|---|
| BearSSL | `79c060eea3eea1257797f15ea1608a9a9923aa6f` | `4becb3d46bc73dddeff548c34166206cc4ace70bfa73cfdd28654108e3ae6143` |
| libtomcrypt | `b96e96cf8b22a931e8e91098ac37bc72f9e2f033` | `91d03f8875326e6e12dfb0bd3f6febaca8ebd3168915a790ccdbb6e35ee73543` |
| isa-l_crypto | `07d0b253843ace996aa5a44f538c9b83b085aba6` | `e4a6105f7eaeecf1425e66b3c0d1a32518d1b7d7f6acd368a0f1446399e85198` |

The upstream `artifacts/fw5/*.elf` binary was intentionally removed from this
tree. It was older than the supplied source snapshot and is not an input to the
HyperHEN build. `scripts/build_hypercore.sh` compiles the current sources and
stages the resulting ELF for build-time embedding.

HyperHEN applies a source-only branding overlay to user notifications and
diagnostic stage labels. It also recognizes the embedded Byepervisor readiness
marker on its exact 13.00, 13.20, and 13.60 route: those revisions have no
supplied A53/PPR profile, so the loader continues into kstuff only after the
hypervisor stage completed. The same handoff accepts the dedicated 5.xx marker
only on 5.00, 5.02, 5.10, and 5.50. A53/PPR remains mandatory on every other
non-legacy route.
Upstream process names, build artifact names, lock/log paths, and protocol
identifiers remain unchanged where renaming could break compatibility or
runtime state detection.
