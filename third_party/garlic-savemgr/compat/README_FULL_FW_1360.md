# Garlic SaveMgr v1.7 — complete firmware offset matrix through 13.60

This package fixes the previous 13.xx-only source patch. It was rebuilt from
**the original user-provided garlic-savemgr-1.7(1).zip**, not layered over the
incomplete patch. The source of all offsets is the **official PS5 Payload SDK
v0.44** `crt/kernel.c`: <https://github.com/ps5-payload-dev/sdk/blob/v0.44/crt/kernel.c>.

## Evidence of previously missing firmware offsets

The user-supplied `garlic-savemgr(1).elf`, disassembled at `__kernel_init`,
contains exact firmware comparisons from 1.00 through **10.60**. It has no
11.xx, 12.xx or 13.xx case. The **15 missing profiles** are:

11.00, 11.20, 11.40, 11.60, 12.00, 12.02, 12.20, 12.40, 12.60, 12.70, 13.00, 13.20, 13.40, 13.42, 13.60

`compat/firmware_offsets_1xx_1360.json` contains **all 67 exact SDK v0.44
profiles** (52 pre-existing in original ELF, 15 newly covered by building
against v0.44 CRT). It contains 5 critical address deltas and 2 vmspace layout
offsets per exact profile. Address deltas are relative to the loader-provided
`KERNEL_ADDRESS_DATA_BASE`, not absolute memory addresses. A firmware version
**not** explicitly listed here is rejected instead of borrowing an adjacent
firmware profile.

## What is changed

- `src/fw_offsets_guard.h`: 67 exact profiles; checks SDK-resolved addresses.
- `src/main.c`: includes header and **actually calls**
  `garlic_fw_offsets_preflight()` at the start of `main()` (fixed a missing call
  in the previous 13xx patch). Save functions, HTML, and SQLite are preserved.
- `Makefile`: adds `make check-offsets` with 67 positive and negative checks.
- `.github/workflows/`: pins SDK v0.44 (including its `crt1.o` profile table).
- `compat/`: documentation, manifest, SHA-256, evidence of original ELF.

## IMPORTANT CRT integration boundary

The original Garlic source does **not** own the firmware switch; it links
the SDK `crt1.o` at build time. **Only a rebuild using SDK v0.44 or another
verified updated CRT can actually add the missing 15 firmware cases to the
ELF.** Merely copying JSON or guarding `main()` does not update the ELF.
The guard executes only after SDK startup; if `__kernel_init` rejects a
firmware before `main()`, it cannot log or correct that failure.

### Build and host test

```bash
make check-offsets
make clean
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk -j4
readelf -Ws garlic-savemgr.elf | grep __kernel_init
```

Use the release distribution from
<https://github.com/ps5-payload-dev/sdk/releases/tag/v0.44> and verify its
`target/lib/crt1.o` is linked, not an older SDK. CI uses a fixed release URL.

### Hardware test status

**No new ELF was generated** because the PS5 compiler/SDK distribution was
unavailable here. The original ELF is unchanged. No hardware test of save
listing, key extraction, mounts, decryption or restoring has been performed.
Do not rely on table coverage alone to claim actual PS5 11.xx–13.xx support.
When testing, start with reading/listing only and backups of disposable saves,
then export, then write/restore only on expendable data. Loader/environment
compatibility and privileges are independent requirements.
