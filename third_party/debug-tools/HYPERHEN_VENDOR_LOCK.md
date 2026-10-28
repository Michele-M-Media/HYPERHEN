# HyperHEN embedded debug tools

These PS5 payloads are embedded unchanged and written to
`/data/HyperHEN/tools` during startup. The toolbox launches them through the
private HyperHEN loader and tracks the returned process ID for safe stop.

| Embedded input | Verified upstream revision | Attribution | SHA-256 | Default TCP port |
| --- | --- | --- | --- | ---: |
| `klogsrv-ps5.elf` | [`c1b64bd3`](https://github.com/ps5-payload-dev/klogsrv/commit/c1b64bd3d3b069a09b1a173a82858cba97a5656f) | John Törnblom; port/quiet argument contribution by Al-Azif | `e828ec144231f81547cb58bc7d2c396fa984be0c2295f31364b58017816dcceb` | 3232 |
| `ftpsrv-ps5.elf` | [`23737c47`](https://github.com/ps5-payload-dev/ftpsrv/commit/23737c4738d15b9b9b036665bbf008d8fd6d82d2) | John Törnblom; current-tree contributions from EchoStretch, Al-Azif, Matthew Vanek, and Gezine | `7d4b31c83eae4e056580482a3a074e1db25922efc74ac1e439473b45d71a5938` | 2121 |
| `ps5-fw-spoof.elf` | User-supplied prebuilt | The artifact identifies the `ps5-fw-spoof` project; authorship was not independently verified | `f1754521caa92a6a1ac313a1b6c969ec49d67750e290ba99870958290a0961f0` | n/a |
| `np-fake-signin-ps5.elf` | User-supplied prebuilt | earthonion; artifact also identifies ps5-payload-dev offline activation code | `6a38ee251b2252ae6752bf23f4f22d970d449731cfe69c1f513f18670efd5314` | n/a |
| `nanodns.elf` | User-supplied prebuilt | Attribution not encoded in the supplied artifact | `fcfb7d47c3b295560ea1603dc015f7c4950353ef0faca16428bd425680ef1fc5` | 53 by default |
| `chukei-ps5.elf` | User-supplied prebuilt | Artifact identifies [Al-Azif/chukei-dns](https://github.com/Al-Azif/chukei-dns) | `0cf13e1ed87b57ffa4fdcfca5d9afe1572be29b4f632677cedf17657a972d750` | 53 by default |
| `zftpd-ps5-zhttp-v1.6.0.elf` | User-supplied zFTPd/zHTTP 1.6.0 prebuilt | SeregonWar, as identified inside the artifact | `fc59b60aab2dce9a8bae385c106bfa174048d765c826101355f1f010a789b497` | Runtime-configured |

The two verified upstream projects declare GNU GPL-3.0-or-later. Their hashes were
matched against their current published PS5 release artifacts before
inclusion; changes require a new verification and an updated lock.

The five user-supplied prebuilts are byte-locked here, but their complete
upstream provenance and redistribution licenses have not been independently
verified. Confirm those terms before publishing a binary that embeds them.

LiveShell and PS5Upload were removed from this prebuilt Debug Advanced set.
HyperDeck now builds LiveShell 1.4 and PS5Upload 7.1.5 from their supplied
source snapshots and disables their independent Media-title registration.

The LAN ELF loader is source-built as `hyperhen_debug_elfldr.elf`. It uses TCP
9025 and separate runtime-state files so it cannot collide with HyperHEN's
private loader on 9020 or its bootstrap/recovery route on 9021.

The initial external loader is [ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr),
developed by John Törnblom and licensed GPL-3.0-or-later. Its upstream release
ELF is not embedded here; HyperHEN's isolated 9020 and 9025 variants are built
from the retained in-tree loader implementation.
