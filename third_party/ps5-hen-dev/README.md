# ps5-hen

### Homebrew Enabler for the PlayStation 5.
Defeats the Hypervisor on firmware <= 4.51 and enables supports for homebrew and ps4 fpkg's.

Supports firmwares: 
- 1.xx - `1.00` `1.01` `1.02` `1.05` `1.10` `1.11` `1.12` `1.13` `1.14`

- 2.xx - `2.00` `2.20` `2.25` `2.26` `2.30` `2.50` `2.70`

- 3.xx - `3.00` `3.10` `3.20` `3.21`

- 4.xx - `4.00` `4.02` `4.03` `4.50` `4.51`

## Download

[![Download](https://img.shields.io/badge/Download-ps5--hen.elf-blue?style=for-the-badge)](https://github.com/cragson/ps5-hen/releases/download/dev-latest/ps5-hen.elf)

## How can I use this?
- Download the payload from button above
    - its automatically the latest dev build
- Start elfldr by running UMTX or Y2JB
- Close application
    - Y2JB, UMTX etc. to allow pinning to all cores
- Send the HEN payload and wait until the notification popup occurs
    - netcat, socat, host everything works
- Have fun

## Changelog
- For full changelog see [CHANGELOG.md](CHANGELOG.md)
- Latest **1.3 (04/15/26)**
    - PR: replace std::print with printf macro
    - PR: Simplify compilation on platforms other than x86_64 
    - Feature: Added Testkit/Devkit support

## Known Issues
- All firmwares have crashes after launching multiple ps4 fpkgs (needs to be investigated)
- Currently does not support Restmode
- Speed can be improved for ps4 fpkg loading (already commented out logging)

## Credits
- [fail0verflow](https://github.com/fail0verflow)
- [flat_z](https://github.com/flatz)
- [c0w](https://github.com/c0w-ar)
- [TheOfficialFloW](https://github.com/theofficialflow)
- [EchoStretch](https://github.com/EchoStretch)
- [idlesauce](https://github.com/idlesauce)
- [Z80](https://x.com/ZiL0G80)
- [John Törnblom](https://github.com/john-tornblom)
- [Specter](https://gist.github.com/Cryptogenic)
- [zeco](https://x.com/notnotzecoxao)
- [ChendoChap](https://github.com/ChendoChap)
- everyone who helped testing and debugging

Thanks to every single one of you - without your help it wouldn't be possible.

## How It Works

| Stage | Name | Description |
|-------|------|-------------|
| 0 | Discovery | Detect firmware, locate kernel base, map HV structures |
| 1 | TMR Relaxation | Patch IOMMU for unrestricted memory access |
| 2 | VMCB Discovery | Locate Virtual Machine Control Blocks |
| 3 | VMCB Patching | Disable HV intercepts and nested paging |
| 3b | XOTEXT Removal | Remove execute-only page protections |
| 4 | Verification | Confirm successful HV bypass |
| 5 | Kernel Patching | Apply firmware-specific kernel patches |
| 6 | Kexec Install | Install kernel execution primitive |
| 7 | HEN Payload | Load HEN kernel module for homebrew/ps4 fpkg support |


## Building

Requires the PS5 Payload SDK. 
Set `PS5_PAYLOAD_SDK` to your SDK path.

```bash
make          # build everything
cd hen && make  # build HEN module only
```

## Deploying

```bash
make test     # send payload to PS5
make debug    # deploy with GDB support
```
