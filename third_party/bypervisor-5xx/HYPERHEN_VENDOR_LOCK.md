# HyperHEN vendor lock: Byepervisor 5.xx

Source imported from the user-supplied `5XX-Bypervisor-main.zip` archive.

- Upstream snapshot: `9c182a1622dd43ea56641a7080638af3673a723f`
- Vendored source only; the supplied prebuilt ELF is intentionally excluded.
- Exact firmware profiles: 5.00, 5.02, 5.10, and 5.50.
- HyperHEN builds this stage from source with its supported NPT route enabled,
  embeds it in the bootstrapper, and waits for its completion marker before
  starting the shared HyperCore stage.

`hyperhen_integration.h` is a local readiness-marker overlay. Runtime branding
and completion verification are adapted for HyperHEN; no firmware offsets are
added and the upstream profile set is not expanded.
