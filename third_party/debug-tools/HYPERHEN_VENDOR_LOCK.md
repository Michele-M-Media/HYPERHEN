# HyperHEN embedded debug tools

These two PS5 payloads are embedded unchanged and written to
`/data/HyperHEN/tools` during startup. The toolbox launches them through the
private HyperHEN loader and tracks the returned process ID for safe stop.

| Embedded input | Verified upstream revision | Attribution | SHA-256 | Default TCP port |
| --- | --- | --- | --- | ---: |
| `klogsrv-ps5.elf` | [`c1b64bd3`](https://github.com/ps5-payload-dev/klogsrv/commit/c1b64bd3d3b069a09b1a173a82858cba97a5656f) | John Törnblom; port/quiet argument contribution by Al-Azif | `e828ec144231f81547cb58bc7d2c396fa984be0c2295f31364b58017816dcceb` | 3232 |
| `ftpsrv-ps5.elf` | [`23737c47`](https://github.com/ps5-payload-dev/ftpsrv/commit/23737c4738d15b9b9b036665bbf008d8fd6d82d2) | John Törnblom; current-tree contributions from EchoStretch, Al-Azif, Matthew Vanek, and Gezine | `7d4b31c83eae4e056580482a3a074e1db25922efc74ac1e439473b45d71a5938` | 2121 |

Both upstream projects declare GNU GPL-3.0-or-later. The hashes above were
matched against their current published PS5 release artifacts before
inclusion; changes require a new verification and an updated lock.

The LAN ELF loader is source-built as `hyperhen_debug_elfldr.elf`. It uses TCP
9025 and separate runtime-state files so it cannot collide with HyperHEN's
private loader on 9020 or its bootstrap/recovery route on 9021.

The initial external loader is [ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr),
developed by John Törnblom and licensed GPL-3.0-or-later. Its upstream release
ELF is not embedded here; HyperHEN's isolated 9020 and 9025 variants are built
from the retained in-tree loader implementation.
