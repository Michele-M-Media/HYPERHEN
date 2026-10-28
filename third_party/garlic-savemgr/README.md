# Garlic SaveMgr for PS5

PS5 save decrypt/encrypt/browse with embedded web UI.

## Usage

1. Send `garlic-savemgr.elf` to elfldr
2. Open `http://<ps5-ip>:8082` in your browser on your PC
3. Drag and drop files into the browse tab to add/replace files

## Building

Requires the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).

```sh
make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
```


## Firmware profiles through 13.60 (SDK v0.44)

See [full offset coverage and build instructions](compat/README_FULL_FW_1360.md).
