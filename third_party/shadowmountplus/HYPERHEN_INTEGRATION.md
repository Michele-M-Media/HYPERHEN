# HyperHEN beta9 integration

Source is based on the user-supplied `ShadowMountPlus-1.7beta9.zip`. New beta9
VFS, SMB, native PKG, image scanning, lifecycle and translation functionality is
retained. Kstuff code and all engine binaries remain outside this change.

The isolated build directories are:

- `/workspace/HyperHEN-SMP9/standalone-build-standard` (`HYPERHEN_LITE=0`).
- `/workspace/HyperHEN-SMP9/standalone-build-lite` (`HYPERHEN_LITE=1`).

Both produce `shadowmountplus.elf`, named `hyperhen_mount.elf` at runtime, with
version tag `1.7beta9-hyperhen`. Build them in separate directories; do not reuse
object files across variants. No clean operation against the stable tree is
required. Standard also keeps the old runtime restrictions for 13.40/13.42.
Lite clamps backport/fakelib, emulator replacement and AMPR downloads off on
every config snapshot. Existing config and file paths remain unchanged.
The API retains the previous HyperHEN loopback default (`127.0.0.1:10101`);
an existing explicit LAN bind address in the user's config is still honored.

## Required launch-route proof

Before submitting automount, the R27 bootstrapper must finish its existing
engine readiness checks and write `SM_HYPERHEN_ROUTE_FILE`, defined in
`include/sm_hyperhen_launch.h`. It must clear stale proof when preparing a new
session. The exact text format is:

```
v1 05100000 bypervisor 0 123
```

Fields are protocol version, masked firmware (`fw & 0xffff0000`), route
(`core`, `legacy`, `bypervisor`), lite flag, and current bootstrapper PID. The
bootstrapper remains alive while waiting for readiness. Core requires its
existing `ready/kstuff` marker plus the actual KEKCALL readiness response.
Legacy is limited to 1.xx--4.xx with existing `legacy_hen` proof; Bypervisor
is limited to 5.xx with existing `hypervisor_5xx` proof. Their failure markers
must be absent. Missing, unknown, mismatched or stale route proof fails closed.

No route publishes `ready/automount` until required ShellCore service,
lifecycle watcher, flags monitor, system-ex mount and initial scan have
succeeded. The configured API must start before readiness. The verified
ShellCore bridge is mandatory except for the five exact limited profiles
listed below. This validates required operations instead of interpreting
absent Kstuff as another engine.

The original beta9 remote-mlock prerequisite on 8.40+ is unchanged. The legacy
1.xx--5.xx bridge uses the upstream pre-8.40 debug API path. Firmware-specific
runtime operation still requires hardware testing; a successful build does
not establish full compatibility of an alternative engine.

The supplied ShellCore offset table has no exact entries for 1.05, 1.10, 1.11,
1.13 or 4.02. `sm_shellcore_remote_resolve()` returns false on these firmwares,
so upstream `sm_shellcore_hooks_start()` also returns false. No offsets or
aliases have been added. The full beta9 on-demand runtime cannot be declared
ready there. For these five exact profiles, a failed bridge starts the existing
scanner/API/direct-registration path where supported and writes
`ready/automount_limited` before publishing `ready/automount`. This mode logs
its limitation and retains the existing localized hooks-failed notification;
it never reports on-demand hooks as available. The limited marker is removed
on startup, shutdown and failure. Bridge failure on every other firmware
remains fatal. The outer R27 integration must report the limited marker while
preserving its working engine and UI.

The bundled link-only fallback `src/libkernel_sys_sdk_compat.c` contains the
1,281 exported symbol names read from the installed SDK's
`target/lib/libkernel_sys.so`. It is used only when that SDK package omits
`src/sce_stubs/libkernel_sys.c`. No syscall implementation, NID guess or kernel
offset has been added; the provided `libkernel_sys_ext.c` is unchanged.
