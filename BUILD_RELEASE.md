# Building HyperHEN 0.9.4

This snapshot includes the dashboard/tool additions and firmware, mount,
localization and lifecycle changes made after the public 0.9.2 tag. The
[release notes](RELEASE_NOTES.md) describe that complete publication
comparison. The recipe below rebuilds the first-party 0.9.4 interfaces using
the retained engine inputs selected for R45, including Automount beta9 and the corrected 13.60 runtime. The product version is 0.9.4; the Hyper worker remains the exact R45 worker.

Use Linux or WSL2. Extract the source archive into the Linux filesystem and keep
`source/`, `scripts/`, `assets/`, `third_party/`, `release-components/`,
`release-inputs/`, and `VERSION` together. The helper resolves every project
path relative to its location; no original developer workspace is required.

For a Git checkout, download
[HyperHEN-v0.9.4-build-inputs.zip](https://github.com/Michele-M-Media/HYPERHEN/releases/download/HYPERHEN_V0_9_4/HyperHEN-v0.9.4-build-inputs.zip)
from the same release and extract it at the checkout root. It supplies `release-components/`
and `release-inputs/`; the Git tree retains their checksum manifests. The full
source ZIP already includes these directories. The helper also stages five
verified runtime tools into their untracked `third_party/debug-tools/`
filenames when a thin checkout does not contain them. A different existing
local tool is rejected rather than overwritten.

## Requirements

- CMake 3.20 or newer, Ninja, Python 3, GNU coreutils and xz/lzma.
- Host LLVM tools selected by the SDK. The observed build used LLVM 18;
  `LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config` selects that installation
  when the SDK's wrappers need an explicit host LLVM path.
- SDK target libcurl with its CA bundle under `target/user/homebrew`, including
  the static dependencies `psl`, `ssl`, `crypto`, `zstd` and `z`, for the Hyper model installer.
- A complete community PS5 Payload SDK with its Prospero compiler/toolchain,
  target headers, system-library stubs and C++ runtime: `libc++.a`,
  `libc++abi.a`, `libunwind.a`, and `target/include/c++/v1`.

The full SDK is not bundled. The build-input ZIP contains the isolated, checksum-pinned CRT object used by the release. The helper links it explicitly after application objects/libraries with `-nostartfiles`; it never falls back to the installed default CRT or edits the SDK. `scripts/prepare_isolated_crt.py` contains the narrowly scoped adaptation source and requires an exact original-object checksum.

The SDK is not bundled. The observed release environment used Clang 18.1.8,
CMake 4.2.3 and Ninja 1.13.2. These are environment observations, not a claim
that an independently identified SDK release has been pinned.

```bash
export PS5_PAYLOAD_SDK=/absolute/path/to/ps5-payload-sdk
bash scripts/build_release.sh --jobs 6
```

Output: `build-release/bin/HyperHEN.elf`.

Use `--build-dir '/path/with spaces/build'` to select another build tree, or
`--configure-only` to validate the inputs and configure without compiling.
The helper does not delete build trees, download dependencies or edit the SDK.
The supplied checksums must pass before configuration.

## Hyper worker and source dependencies

The default release build embeds the verified R45 worker unchanged. An optional
`python3 scripts/build_hyper_worker.py --jobs 4` rebuilds its first-party sources
against seven checksum-pinned static AI libraries. See [the worker notes](source/hyper/README.md)
for the relocatable recipe, exact upstream revisions, changed translation units
and the corresponding-source archive included in the build inputs. No model
weights are bundled; they are downloaded separately from the console menu.

## CI scope

Pushes and pull requests run source metadata, catalog JSON and host-side build
helper checks without downloading the retained ELF inputs or a console SDK.
The former full-component nightly build is now manual-only and remains a
separate pipeline from this release recipe.

A published release can run a compatible native rebuild when repository
variables `PS5_PAYLOAD_SDK_ARCHIVE_URL` and `PS5_PAYLOAD_SDK_ARCHIVE_SHA256`
identify a complete, checksum-pinned SDK ZIP. The workflow obtains the retained
inputs from that release's build-inputs asset using its read-only GitHub token
and compares their manifests with the checked-out release source. Without
those SDK variables, it records a notice and skips the native job; source/host
checks are not presented as a successful native rebuild. Manual dispatch can
also provide a release tag, SDK ZIP URL and SHA256 explicitly.

Publishing with an Actions `GITHUB_TOKEN` does not trigger a second workflow
from the resulting release event. For that publication path, use manual
`workflow_dispatch` to request the optional native rebuild; the source checks
on the source commit are separate.

## What this build rebuilds

The release-compatible recipe rebuilds the first-party daemon, ShellUI,
HyperDeck wrapper, bootstrapper and unpacker. It retains the established
firmware engines, Automount beta9, utilities and private loaders selected for
R45. The integrated R45 Hyper worker, voice-data archive and restored 13.xx compatibility core are explicit, checksum-pinned inputs. ShellUI and HyperDeck are rebuilt so their product metadata is `v0.9.4`, while
its five embedded native tools remain the retained inputs.

`hyperhen_elfldr_inner.elf` is the private recovery loader inside the daemon;
it is intentionally distinct from the external `hyperhen_elfldr.elf` input.
`hyperdeck-pkg-helper.elf` is the retained package-install helper. The other four
HyperDeck engines and the HTTP library/header are staged from `release-inputs/`
into the selected build tree by the helper.

Keep prebuilt inputs and their provenance explicit. This package includes
vendored source plus retained binary dependencies; it is not a claim that
every dependency can be rebuilt exclusively from the included source.
Read the component inventory and upstream license notices before redistributing.

The historical `scripts/build.sh` is a broader component-source build and is
not this release recipe. It may rebuild additional engines or fetch missing
SDK runtime files. Rebuilding Automount separately also requires the adapted
beta9 source and its link-stub compatibility declarations for the SDK used.
Those declarations link to the real `libkernel_sys.sprx`; they are not
executable replacement syscall implementations.

Toolchain, build paths, version metadata and compression tools can affect
binary output. No bit-for-bit reproducible rebuild claim is made.
