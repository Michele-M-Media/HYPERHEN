# Byepervisor 9.40 binary lock

HyperHEN keeps the supplied 9.40 payload as a dedicated, immutable stage. It
is selected only for exact firmware 9.40 and is never used as a fallback for
another firmware.

- SHA-256: `7b592477a27524ae8fcfc491024aaf3befc5d7214596170d63aa43340a74e688`
- Size: `159936` bytes
- Format: little-endian x86-64 PIE ELF
- Hardware selection: performed internally by the payload for regular and Pro
  models
- Provenance: binary supplied for HyperHEN integration; matching source and a
  standalone license file were not included

The payload writes `/data/phu_hv_result.txt` both before and after suspend. The
bootstrapper therefore requires a fresh, changed report containing the exact
post-resume full-success token before it continues to the dedicated no-PPR
HyperCore image. Merely sending the ELF,
finding the process, or seeing the report file is never treated as readiness.

Confirm the upstream license and provenance before redistribution.
