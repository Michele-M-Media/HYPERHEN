# Third-party dependencies

For the complete public component inventory, pinned revisions, firmware-route
matrix, and inclusion boundaries, see
[`../docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md`](../docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md)
and [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

Everything in this directory is maintained outside HyperHEN. Keeping external
code here makes `source/` exclusively first-party code.

| Path | Form | Role in HyperHEN |
|------|------|------------------|
| [`7zip_sdk/`](7zip_sdk/) | Vendored source | LZMA support used by the unpacker |
| [`cjson/`](cjson/) | Vendored source | JSON parsing and serialization |
| [`cheat_support/`](cheat_support/) | Vendored source | AES, base64, miniz and SHA-256 used by cheat parsers |
| [`keystone/`](keystone/) | Headers + prebuilt archive | ShnExt assembly support |
| [`kstuff-a53/`](kstuff-a53/) | Vendored source snapshot | Builds the mandatory embedded HyperCore (kstuff + A53/PPR + BackPork) |
| [`byepervisor-reborn/`](byepervisor-reborn/) | Vendored source snapshot | Builds the embedded 13.00/13.20/13.60 hypervisor stage |
| [`bypervisor-5xx/`](bypervisor-5xx/) | Vendored source snapshot | Builds the embedded 5.00/5.02/5.10/5.50 hypervisor stage |
| [`ps5-hen-dev/`](ps5-hen-dev/) | Vendored source snapshot | Builds the embedded legacy HEN engine for the exact 1.xx–4.51 profile set |
| [`shadowmountplus/`](shadowmountplus/) | Vendored source snapshot | Builds the embedded HyperHEN Automount service and optional ShellCore bridge |
| [`hyperdpi/`](hyperdpi/) | Vendored source snapshot + HyperHEN overlay | Builds the embedded HyperDPI browser package installer |
| [`hyperhen-plugin-sdk/`](hyperhen-plugin-sdk/) | Vendored source snapshot + socket-path overlay | Builds the pinned plugin client compatibility runtime used by HyperDPI |
| [`hyperlinux/`](hyperlinux/) | Vendored source snapshot + narrow HyperHEN overlay | Builds the embedded one-shot Linux boot engine from pLinux5-NG and ps5-linux-loader |

Third-party file names retain their upstream spelling even when it differs
from the project's snake_case convention. This keeps upstream updates easy to
review.

The three upstream Gitlinks inside `kstuff-a53` are vendored at their exact
revisions; see [`kstuff-a53/HYPERHEN_VENDOR_LOCK.md`](kstuff-a53/HYPERHEN_VENDOR_LOCK.md).
The supplied Byepervisor revision and local orchestration overlay are recorded
in [`byepervisor-reborn/HYPERHEN_VENDOR_LOCK.md`](byepervisor-reborn/HYPERHEN_VENDOR_LOCK.md).
The additional 5.xx, 1.xx–4.51, ShadowMountPlus, HyperDPI, and HyperLinux source snapshots and their
local integration boundaries are recorded in their respective
`HYPERHEN_VENDOR_LOCK.md` files.
`scripts/sync_dependencies.sh` verifies the snapshot. The build never downloads
or substitutes a release `kstuff.elf`.

## Runtime-only external dependency

[ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr) on port 9021
is supplied by the runtime environment for the initial bootstrap.

```bash
./scripts/sync_dependencies.sh
```
