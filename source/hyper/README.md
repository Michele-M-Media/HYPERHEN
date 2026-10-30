# Hyper worker (R45)

This is the local speech-to-text, dialogue and speech-synthesis worker embedded
in HyperHEN 0.9.4. `runtime/` contains the R45 first-party sources (the worker header include is made relative to the published include root; runtime behavior is unchanged) and
`streaming/` contains its reusable 512 MiB weight arena. Model weights remain
external in `/data/Hyper`; no model is included in this source tree or the ELF.

The smallest model is the confirmed working voice configuration. Larger models
remain experimental. The latest 7B console test loaded successfully but reached
the 90-second reply deadline before speech playback. Compilation or the host
tests do not establish usable real-time performance on PS5. No GPU backend is
enabled in this version.

## Build

The ordinary `scripts/build_release.sh` preserves the checksum-pinned R45 worker.
To rebuild that worker separately with the same retained seven AI libraries:

```bash
export PS5_PAYLOAD_SDK=/absolute/path/to/ps5-payload-sdk
python3 scripts/build_hyper_worker.py --check-only
python3 scripts/build_hyper_worker.py --jobs 4
```

The versioned build-input ZIP must be extracted first. The recipe verifies the
AI libraries and the isolated CRT and links that CRT last. It never modifies
the installed SDK. The output is `build-hyper-worker/Hyper-Assistant-R45.elf`.
The recipe is relocatable; no original development workspace is required.

## AI source and provenance

`upstream-lock.json` records the exact llama.cpp, whisper.cpp and eSpeak NG
source archive URLs, revisions and SHA256 values. The corresponding complete
source snapshot is included in the build-input asset as
`release-inputs/hyper-ai/upstream-source.tar.gz`. Licenses and the headers used
by this adapter are in `third_party/hyper-ai/`.

`patches/llama-model-loader.cpp` and `patches/ggml-cpu.cpp` are the adapted
translation units used by the retained AI libraries. They provide bounded
weight loading and execution for the 7B path. The loader is compiled with
`HYPER_PS5_SERIAL_TENSOR_VALIDATION=1`; the other patched unit uses the upstream
CPU target flags and includes `streaming/hyper_weight_stream.h`. The fixed arena
provider itself is linked into the worker. These sources accompany the frozen
library inputs; the optional worker recipe does not rebuild the entire AI stack.

The speech, dialogue and voice engines are upstream projects, credited under
their original licenses. Hyper's integration, data installer, model selection,
session lifecycle and DualSense bridge are separate project components.
