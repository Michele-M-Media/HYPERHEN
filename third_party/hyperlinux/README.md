# pLinux5-NG

boot linux on PS5 consoles running high firmware (8.xx through 13.60) 

this project is a port/rework of the existing [ps5-linux-loader](https://github.com/ps5-linux/ps5-linux-loader) 
(which currently works on 3.00 though 7.61) to the new [byepervisor-reborn](https://github.com/PS5Dev/Byepervisor-Reborn) hypervisor escape 

- the linux boot pat itself (bzImage/initrd relocation, `boot_params`/e820 setup,
  hardware bring up, SVM/IOMMU teardown, the jump to `startup_64`) is brought over
  from the existing ps5-linux-loader
- the **escape* -- how we get code running in hypervisor root mode on all cores on high firmware --
  is what has changed in this project. on 3.00 through 7.61 its flatz' ACPI-wakeup ROP
  chain; on high firmware it is a khook on the S3-resume `HV_MP_STARTUP_CALL` path. as implemented
  in Byepervisor-Reborn.

## usage

### **CRITICAL** 
ps5 configuration:
  enable rest mode features:
    `settings` -> `system` -> `power saving` -> `features available in rest mode`, set `Supply Power to USB Ports` to `Always`
  disable HDMI device link:
    `settings` -> `system` -> `HDMI`, set `Enable HDMI Device Link` to be `off` 
  

# installation

follow [ps5-linux-loader installation](https://github.com/ps5-linux/ps5-linux-loader#installation) until you get to
step 4, then come back here.

### running the jailbreak

- set your playstations DNS under:
  `settings` -> `network` -> `settings` -> `set up internet connection` then click the `options` button on
  your controller, then select `advanced settings`, from there set `primary DNS` to `45.56.67.85` then select `ok`
  ***you will get a popup saying `internet connection failed`, thats good, continue.***

- navigate to the users guide:
  `settings` -> `Guide & Tips, Health and Safety, and Other Information` -> `User's Guide`.
   when a popup appears saying: `the secuirty of the page can't be confirmed...`, click `yes`

    you should end up in a page with big letters saying `RELAPSE`, click the `Jailbreak` button & wait for it to finish
    it finishing will be marked by elfldr starting.
now download the `pLinux5-NG.elf` from releases, or see [building](#building) to build from source, then move to [sending](###sending)

### sending

on linux/unix, use:
```bash
# (from the area with the repo + pLinux5-NG.elf) 
cd scripts/
chmod +x send.sh
./send.sh PS5_IP=<Playstation 5s IP> PS5_PORT=9021 pLinux5-NG.elf
```




# building
Requires the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) and the
pLabs5 host toolchain override:

```bash
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
export LLVM_CONFIG=/path/to/pLabs5-SDK/tools/nixos/llvm-config
make -j
```

The output is `bin/pLinux5-NG.elf`.

The shellcode blobs are compiled freestanding with the host `gcc`
(`-ffreestanding -nostdlib`) and embedded as C headers; `scripts/bin2c.py`
replaces `xxd -i`, which is not present in every environment.

## Sending

```bash
socat -t 99999999 - TCP:$PS5IP:9021 < bin/pLinux5-NG.elf
# or
nc -N $PS5IP 9021 < bin/pLinux5-NG.elf
```

## License

The project is licensed under the **AGPL-3.0** (see [`LICENSE`](LICENSE)).
The boot path and shellcode are derived from **ps5-linux-loader**, which is
**GPL-3.0**; those files retain their original authorship and are used here in
accordance with the GPL/AGPL compatibility provisions. The high-firmware escape
is adapted from **Byepervisor-Reborn**. See [`PORTING.md`](PORTING.md#credits).

## legal
I, niko86c/pLabs5, as well as its contributers, are not affiliated with, associated with, sponsored by, endorsed by or otherwise established with Sony Interactive Entertainment, Playstation, or any of their other companies or works unless explictly stated otherwise.
***Just because a explict mention above isn't present does **NOT** mean otherwise.***

All software is provided "as is", without warranty of **ANY** kind, express
or implied. Use all software at your own risk.

You are solely responsible for complying with terms of service of all programs, as well as any applicable law. 
