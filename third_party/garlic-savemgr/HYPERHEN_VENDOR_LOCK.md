# HyperHEN vendor lock — Garlic SaveMgr 1.7

This directory is the user-supplied Garlic SaveMgr 1.7 source snapshot. It is
built locally as `garlic-savemgr.elf` and embedded in HyperHEN; no prebuilt
Garlic ELF is shipped as an input.

- Archive SHA-256:
  `c672888d693c47d6926489534b58b6e2e615d605e57b3160dcbcdf337dca75f4`
- Attribution retained from the supplied source: **earthonion**.
- SQLite remains the supplied amalgamation (public-domain upstream project).
- The snapshot declares exact firmware profiles from 1.00 through 13.60. The
  runtime keeps the supplied fail-closed profile guard; it does not select a
  nearest firmware profile.

The source archive did not include a license file. Its attribution and the
supplied compatibility material are retained verbatim pending an upstream
license statement. `compat/ORIGINAL_SOURCE_SHA256.txt` records that this is a
compatibility-patched source snapshot rather than a byte-identical upstream
copy.

HyperHEN materializes the service at `/data/HyperHEN/tools/garlic-savemgr.elf`.
The Advanced Debug toggle asks its local shutdown endpoint to unmount and sync
before falling back to the existing process-stop mechanism.
