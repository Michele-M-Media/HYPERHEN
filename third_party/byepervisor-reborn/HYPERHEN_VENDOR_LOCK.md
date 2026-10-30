# HyperHEN vendor lock

This directory is the supplied Byepervisor Reborn source snapshot. Its archive
comment identifies commit:

`ea04f7132b837087f552c57261cb800481a3a910`

HyperHEN builds the payload from source and embeds it in the bootstrapper. The
exploit primitives and firmware offset maps remain upstream code. The local
integration overlay adds HyperHEN branding, process/readiness markers,
fail-closed unsupported-firmware handling, and removes the upstream
demonstration write to kernel text. It also hardens the Rest Mode handoff:
page-table walks reject absent entries, every mapping change is read-back
verified, and completion is published only after all discovered VMCBs confirm
that nested paging is disabled.

The locked base snapshot fully covers firmware 13.00, 13.20, and 13.60. No
unsupported firmware is routed into this stage.

The later user-supplied `Byepervisor-Reborn-fw-12.70.zip` identifies upstream
commit `0c353514d90d51d9f1233586b33620355833ae93`. It contributes only its exact
12.70 offset profile and dispatch. The imported header is byte-identical to the
supplied file (SHA-256
`8768441ab5023a391820cae2b56dcf14a0470f05ceb6404ed43015c15f42e007`),
whose table is attributed upstream to EchoStretch. Its 13.00, 13.20, and
13.60 headers are byte-identical to this directory's locked versions.

The archive's standalone lifecycle changes are not merged: they remove the
HyperHEN readiness adapter and several completion/read-back checks. Firmware
12.70 therefore uses the existing hardened trigger/success/failure protocol,
page-table verification, and Rest Mode handoff used by the 13.xx profiles.
No automatic standby call from the archive is included.

The 12.70 integration is isolated in the final payload as
`hypervisor_1270` and its matching `hypercore_1270`. Those are the only two
new embedded component slots relative to the trusted prior layout;
`hypercore_940` and every common firmware component retain their prior bytes.

## Binary-reference audit

The user-supplied `byepervisor_reborn.elf` was originally inspected as a
binary reference and is not the HyperHEN-adapted payload embedded in the
bootstrapper. Starting with v0.9.4, an unchanged copy is also distributed as
the separate release attachment `Byepervisor-Reborn.elf` for the manual
WebKit -> Bypervisor -> Rest Mode -> resume -> WebKit -> HyperHEN procedure.
The separate file retains its upstream identity and attribution to
[PS5Dev / Byepervisor Reborn](https://github.com/PS5Dev/Byepervisor-Reborn)
and its contributors. Renaming the attachment does not change its bytes.
The hash and 13.xx offset-map evidence below identify that exact original;
it must not be confused with the dedicated 12.70 or AUTO 9.40 attachments.

- ELF SHA-256:
  `a8fa2970f55cd29454211f618aa3145341da7959b4d15a60b8f665cd4a8b112d`
- Its 13.00, 13.20, and 13.60 firmware offset-map values match the locked base
  source snapshot. This older ELF predates the 12.70 port and is not evidence
  for that profile.
- The embedded supervisor payload matches the source-built payload bytes.

`HV-Bypass-13.60-1.0.0.zip` was audited as supporting research material. It
does not provide a complete replacement Byepervisor source build, so its
experimental probes and reference material are deliberately not vendored into
the HyperHEN runtime.
