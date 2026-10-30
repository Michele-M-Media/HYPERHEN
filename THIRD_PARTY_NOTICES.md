# Third-party notices

HyperHEN combines original integration work with third-party source. The root
GPL-3.0 license applies to HyperHEN's own GPL-covered work; it does not replace
the copyright or license terms attached to vendored files.

The detailed component, revision, inclusion, and firmware matrix is in
[`docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md`](docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md).

## License files retained in the tree

| Component | Location | Terms recorded by the supplied snapshot |
| --- | --- | --- |
| HyperHEN | [`LICENSE`](LICENSE) | GNU GPL-3.0 |
| HyperCore / kstuff-a53 | [`third_party/kstuff-a53/LICENSE`](third_party/kstuff-a53/LICENSE) | GNU GPL-3.0 |
| BearSSL | [`third_party/kstuff-a53/ps5-kstuff/BearSSL/LICENSE.txt`](third_party/kstuff-a53/ps5-kstuff/BearSSL/LICENSE.txt) | BearSSL upstream license |
| LibTomCrypt | [`third_party/kstuff-a53/ps5-kstuff/libtomcrypt/LICENSE`](third_party/kstuff-a53/ps5-kstuff/libtomcrypt/LICENSE) | LibTomCrypt upstream license |
| ISA-L Crypto | [`third_party/kstuff-a53/ps5-kstuff/isa-l_crypto/LICENSE`](third_party/kstuff-a53/ps5-kstuff/isa-l_crypto/LICENSE) | ISA-L Crypto upstream license |
| ShadowMountPlus | [`third_party/shadowmountplus/LICENSE`](third_party/shadowmountplus/LICENSE) | GNU GPL-3.0 |
| HyperDPI | [`third_party/hyperdpi/LICENSE`](third_party/hyperdpi/LICENSE) | GNU GPL-3.0 |
| HyperHEN plugin client runtime | [`third_party/hyperhen-plugin-sdk/LICENSE`](third_party/hyperhen-plugin-sdk/LICENSE) | GNU GPL-3.0 |
| HyperLinux / pLinux5-NG | [`third_party/hyperlinux/LICENSE`](third_party/hyperlinux/LICENSE) | GNU AGPL-3.0, with retained GPL-3.0 components noted by the upstream sources |
| cJSON | License header in each file under `third_party/cjson/` | MIT |
| 7-Zip LZMA SDK | Public-domain notice in `third_party/7zip_sdk/C/7zVersion.h` | Public domain as stated upstream |
| miniz | License and Unlicense text retained in `third_party/cheat_support/miniz.c` and `.h` | MIT / Unlicense as stated in those files |
| Base64 helper | Header retained in `third_party/cheat_support/base64.c` | BSD terms stated in the source |

## Embedded debug-service payloads

The following upstream PS5 payload images are redistributed unchanged under
their GNU GPL-3.0-or-later notices. Their exact SHA-256 values and default
ports are recorded in
[`third_party/debug-tools/HYPERHEN_VENDOR_LOCK.md`](third_party/debug-tools/HYPERHEN_VENDOR_LOCK.md).

| Component | Upstream and attribution | Inclusion |
| --- | --- | --- |
| `klogsrv-ps5.elf` | [ps5-payload-dev/klogsrv](https://github.com/ps5-payload-dev/klogsrv), John Törnblom; current port/quiet argument contribution by Al-Azif | Embedded unchanged |
| `ftpsrv-ps5.elf` | [ps5-payload-dev/ftpsrv](https://github.com/ps5-payload-dev/ftpsrv), John Törnblom; current-tree contributions from EchoStretch, Al-Azif, Matthew Vanek, and Gezine | Embedded unchanged |

The additional user-supplied debug prebuilts (`ps5-fw-spoof`,
`np-fake-signin`, NanoDNS, Chukei DNS, and zFTPd/zHTTP)
are embedded unchanged and byte-locked in the same vendor record. Their full
upstream license and redistribution provenance must be confirmed before a
public binary release.

The external first-hop [elfldr](https://github.com/ps5-payload-dev/elfldr) is
also developed by John Törnblom and distributed upstream under GPL-3.0-or-later.
HyperHEN does not embed its current upstream release ELF; it builds its private
and debug loader variants from the retained in-tree loader implementation.

## Source snapshots requiring license confirmation

The following supplied snapshots or binary development dependencies do not
include a standalone license file at their vendored root:

- `third_party/byepervisor-reborn/`
- `third_party/bypervisor-5xx/`
- `third_party/ps5-hen-dev/`
- `third_party/hyperlinux-1360/1360linux.elf`
- `third_party/byepervisor-940/byepervisor-940.elf`
- the prebuilt Keystone archive under `third_party/keystone/`

Their upstream redistribution terms and the exact Keystone build provenance
must be confirmed before public binary or source redistribution. Add the
matching upstream license and copyright notices without removing any existing
file-level notices.

## External and reference-only projects

The PS5 Payload SDK and the current upstream `elfldr` release ELF are external
dependencies and are not copied into the HyperHEN release. GoldHEN and
a53-code-exec are design or research references and are not embedded. The
optional onionHEN FTP plugin is installed separately by the user. Their
respective upstream terms apply.

HyperDeck revision 10 builds and embeds the supplied PS5 Game Compressor 1.0.4,
PS5 Web File Manager 1.11, CheatRunner 0.18, and PS5Upload 7.1.5 source trees.
Their original frontends, backend functions and native API
layouts are retained. HyperDeck
starts each complete service on demand on its own native port; only the code
path that creates that project's separate Media title is excluded. The source,
license and README material is retained under `third_party/hyperdeck-tools/`.
LiveShell 1.4 source and its GPL-3.0 notice remain vendored for research, but
its renderer, daemon, and web interface are not compiled or embedded in this
release after hardware compatibility testing failed.
The supplied Game Compressor snapshot did not include a standalone license
file, so public redistribution permission must be confirmed with its author
before shipping an ELF that embeds that component. LiveShell retains its
GPL-3.0 notice; PS5Upload retains its supplied license and license exceptions.

## Preservation rule

Do not remove or rewrite upstream copyright headers, license files, commit
locks, or source-level provenance notes. When updating a vendored component,
update its `HYPERHEN_VENDOR_LOCK.md`, this notice, and the public technical
inventory in the same change.


## Hyper local voice assistant (0.9.4)

Hyper uses [llama.cpp](https://github.com/ggml-org/llama.cpp) and its GGML CPU
runtime for local dialogue, [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
for local speech recognition, and [eSpeak NG](https://github.com/espeak-ng/espeak-ng)
for speech synthesis and voice data. Their original license files are included
under `third_party/hyper-ai/`; exact source revisions and archive checksums are
recorded in `source/hyper/upstream-lock.json`. The complete corresponding source
snapshot is included in the release build-input asset at
`release-inputs/hyper-ai/upstream-source.tar.gz`; adapted loader/CPU translation
units are published in `source/hyper/patches/`.

Model weights are separate downloads and retain their respective upstream
licenses. In particular, the catalogue includes Qwen2.5-family models;
a catalogue entry does not imply a hardware validation
or a change to the model author's terms. No model weights are embedded in the
HyperHEN ELF.
