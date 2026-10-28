# websrv 0.34 integration

Source: user-supplied `websrv-0.34.zip`, archive comment commit
`4d35610914273f8f4fc37af2b1d5bfb069dfba5b`.
Upstream: https://github.com/ps5-payload-dev/websrv
Copyright John Törnblom; GPL-3.0-or-later. Original notices retained.

The original asset registry and generator are included here. HyperDeck's
dedicated server and Media launcher live in `source/hyperdeck/`. The launcher
is adapted from upstream `src/ps5/sys.c` (install_app/install_launcher).
No generic filesystem, SMB, upload, arbitrary-ELF, remote-kill or title-launch
HTTP endpoints are enabled. The HTTP listener is loopback-only on port 8088;
it does not replace or terminate an existing upstream websrv on port 8080.
Only exact HyperHEN firmware routes are accepted. No firmware offsets added.

Revision 6 additionally includes `homebrew/PKGInstall/pkg_install.c`, derived
from the same supplied websrv 0.34 archive. Copyright (C) 2025 John Törnblom,
GPL-3.0-or-later. Its licence and attribution are retained; the local ABI
declarations are checked at build time for the HyperHEN integration.
The new `/hbldr` route is restricted to this embedded helper and inventoried
package paths, with the existing local Host/origin/session checks. Generic
ELF execution, network URLs and filesystem endpoints remain disabled.
PIZZA HEN's v2.00 catalogue/HTML conventions informed `pkg_catalog.cpp` and the
new page; reference commit `21948a05c49c4239059af4eb697f9a577348a899`.
