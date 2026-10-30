# HyperHEN v0.9.4

HyperHEN 0.9.4 brings Hyper to the main Toolbox page and carries forward the
R45 development fixes, including the restored 13.xx Bypervisor HyperCore path
and corrected 13.60 runtime. Automount remains ShadowMountPlus 1.7 beta9.

## Hyper — a local voice assistant for the DualSense

- Hold **L1 + R1 + □** to call Hyper, speak through the DualSense microphone
  and hear its response through the controller speaker.
- **Hyper — Voice assistant** appears directly below **Content Install & Management**
  on the main Toolbox page. Enable/disable it, install models, choose the
  active model and select a speaking voice from this page.
- Hyper is enabled by default; its saved switch can turn it off. The worker
  is integrated, so there is no separate P14 ELF to start.
- The installer downloads models to `/data/Hyper/models` and installs the
  required voice data under `/data/Hyper`. Model data is not packed into the
  main ELF. Speech recognition, conversation and speech synthesis run on the
  console after installation, without sending voice online or requiring a PC.
- The catalog offers eight Qwen2.5 choices from 0.5B through 7B, plus automatic
  voice selection and 85 voice variants. Menus, notifications and the voice
  language follow the console language.
- **Qwen2.5 0.5B Q4_K_M is the currently working, console-confirmed model.**
  Every other catalog choice remains **experimental / beta**. Larger models
  may be very slow or time out. The latest R45 7B Q5_K_M test loaded and
  generated tokens but reached the reply timeout without a spoken response.
- Readiness now waits for the selected model to load. Session cancellation,
  status notifications and the 7B memory path have received fixes. The R45
  7B path uses a reusable 512 MiB weight cache and a persistent CPU thread
  pool; this cache size is not a total-RAM requirement or a performance claim.

For first use, choose **Download and install models**, wait for **Hyper ready**,
then call Hyper from the dashboard. To test a different model, use **Model to
download → Download selected model**, wait for installation, then change
**Active AI model**. Downloading alone does not switch models. The default
installation downloads about 569 MB and occupies about 600 MB; additional
models require their listed storage plus installation headroom.

See the [Hyper guide in the README](https://github.com/Michele-M-Media/HYPERHEN#hyper--local-voice-assistant-beta)
for the model table, controls and voice settings.

## Firmware and startup continuity

- Restores the public 0.9.2 HyperCore component for the Bypervisor routes on
  **13.00, 13.20 and 13.60**, while retaining **Automount beta9** separately.
- Retains the corrected **13.60 runtime initialization** across the native
  components. The 13.60 correction was confirmed by the tester in the R42
  development build; retained code and local checks do not certify every
  firmware/console combination.
- Preserves HyperDeck, Advanced Debug payload services, the legacy and 5.xx
  selectors, earlier firmware-specific fixes and multilingual notifications.
- Continues the shared **manual WebKit closure** rule and two-second quiet
  interval before Toolbox attachment.
- Updates product labels and notifications to **HyperHEN v0.9.4**.

## Bypervisor profiles: required order

For **9.40**, **12.70**, **13.00**, **13.20** and **13.60**:

1. Open WebKit and launch the matching Bypervisor ELF.
2. Enter Rest Mode.
3. Resume the PS5.
4. Open WebKit again.
5. Launch HyperHEN.
6. Close WebKit manually and leave it closed while Automount and the Toolbox
   complete startup, including the two-second quiet interval.

Bypervisor downloads and the full startup procedure are available in the
[README](https://github.com/Michele-M-Media/HYPERHEN#bypervisor-downloads-and-startup-procedure).

The 5.xx selector keeps its integrated Bypervisor option; no additional 5.xx
file is needed. The 13.40/13.42 lite routes remain separate.

## Thanks

All upstream developer credits are retained in the [README](https://github.com/Michele-M-Media/HYPERHEN#credits),
including the newly documented Hyper components: Whisper, whisper.cpp,
llama.cpp, Qwen2.5 and eSpeak NG.

Special thanks to [**@oG_LuckyXmods**](https://x.com/oG_LuckyXmods) for the
extensive testing support, and to the [**PizzaClub testers**](https://t.me/+6SY-QLIzDkI1Mzg0)
for their time, feedback and continued help with firmware tests.

