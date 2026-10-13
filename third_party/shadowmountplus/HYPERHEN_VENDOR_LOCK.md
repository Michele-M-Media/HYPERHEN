# HyperHEN vendor lock: ShadowMountPlus

Source imported from the user-supplied `ShadowMountPlus-1.7-beta5fix1.zip`
archive.

- Upstream snapshot: `98c524890176c7b474fcb03387bec88c4a559861`
- Archive SHA-256:
  `c2f78c312d4fc8aa95239d3824300e4d4103677490d83db2b1e698a9fe7c9cd0`
- License: GNU GPL-3.0; the upstream `LICENSE` remains alongside the source.
- Build output: `hyperhen_automount.elf`, compiled locally from this snapshot
  and embedded in the HyperHEN bootstrapper.

The upstream source contains 62 exact optional SceShellCore bridge profiles,
from 1.00 through 13.60. HyperHEN does not synthesize offsets for firmware
revisions absent from that table. The base scanner and mount engine is kept
separate from those optional hooks and is started only after the selected HEN
or HyperCore route reports readiness.

Local integration changes are intentionally limited to the HyperHEN process
name, readiness/failure markers, visible product identity, and mount-lifecycle
orchestration. HyperHEN claims `/data/.kstuff_noautomount` before HyperCore
starts; the native HyperCore scanner and integrated BackPork monitor remain
paused while this automount service owns the mount lifecycle, preventing two
providers from mutating the same mounts.

HyperHEN also adds an offset-free pre-exec fallback to the shared lifecycle
watcher. It runs only when the verified SceShellCore bridge is inactive and
uses inherited SysCore child events to prepare the existing package-backport
and fakelib engines before exec. No missing firmware offset is synthesized.
