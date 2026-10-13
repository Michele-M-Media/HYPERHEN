# HyperDPI source integration

HyperDPI is HyperHEN's built-in browser package installer. The implementation
is derived from the GPLv3 `OnionBuddies/onionHEN-dpiv2-plugin` source at commit
`4e80389a87dd709f5f7ba316cbaa23d05a3cafc3`.

HyperHEN builds this source as `hyperdpi.elf`, embeds that intermediate output
in the bootstrapper, and launches it from memory after the daemon's plugin IPC
service is ready. It is not installed as an external plugin file.

The integration changes the product identity, process name, descriptor ID,
configuration/log/storage paths, WebUI branding, and IPC socket path. The
transfer, staging, install, queue, retry, and SSE behavior remains based on the
pinned upstream implementation. The service is disabled by default because
its HTTP listeners do not authenticate clients.

Runtime paths:

- `/data/HyperHEN/HyperDPI.ini`
- `/data/HyperHEN/HyperDPI.log`
- `/data/HyperHEN/HyperDPI-server.log`
- `/user/data/HyperHEN/pkgs`

Default ports are TCP 9090 for the transfer API and TCP 12800 for the WebUI.
Use the service only on a trusted local network.

The original GPLv3 license and third-party notices are retained alongside the
source. `HYPERHEN_VENDOR_LOCK.md` records the exact snapshots and local
integration boundary.
