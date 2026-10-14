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

The supplied offsets fully cover firmware 13.00, 13.20, and 13.60 only. No
unsupported firmware is routed into this stage.

## Binary-reference audit

The user-supplied `byepervisor_reborn.elf` was inspected as a reference only;
it is not embedded or shipped by HyperHEN.

- ELF SHA-256:
  `a8fa2970f55cd29454211f618aa3145341da7959b4d15a60b8f665cd4a8b112d`
- Every supplied firmware offset-map value (13.00, 13.20, and 13.60) matches
  the source snapshot.
- The embedded supervisor payload matches the source-built payload bytes.

`HV-Bypass-13.60-1.0.0.zip` was audited as supporting research material. It
does not provide a complete replacement Byepervisor source build, so its
experimental probes and reference material are deliberately not vendored into
the HyperHEN runtime.
