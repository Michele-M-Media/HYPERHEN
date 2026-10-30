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

See the [Hyper guide in the README](README.md#hyper--local-voice-assistant-beta)
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

All upstream developer credits are retained in the [README](README.md#credits),
including the newly documented Hyper components: Whisper, whisper.cpp,
llama.cpp, Qwen2.5 and eSpeak NG.

Special thanks to [**@oG_LuckyXmods**](https://x.com/oG_LuckyXmods) for the
extensive testing support, and to the [**PizzaClub testers**](https://t.me/+6SY-QLIzDkI1Mzg0)
for their time, feedback and continued help with firmware tests.


---

# HyperHEN v0.9.3

HyperHEN continues to evolve as a single, firmware-aware PS5 homebrew stack.
This release brings together the additions made since public v0.9.2:
HyperDeck, expanded Advanced Debug tools, engine selectors, updated Automount,
broader localized notifications, and improvements to Toolbox startup and
ShellUI recovery.

> [!IMPORTANT]
> **Close WebKit manually and return to the PS5 dashboard.** After choosing an
> engine in the selector, close the browser yourself; HyperHEN does not close
> it automatically. On profiles without a selector, follow the startup
> notifications and close WebKit if it is open. The Toolbox waits until the
> WebKit content process has remained absent for a continuous **two seconds**
> before attaching to ShellUI. This shared startup rule applies to **all 67
> declared profiles**, not just 5.xx. Leaving WebKit open keeps the Toolbox
> waiting.

## What's new

- **HyperDeck Media dashboard:** a native-tool hub combining Game Compressor,
  PS5 Web File Manager, CheatRunner, PS5Upload, and package installation in one
  interface, accessible from its Media-tab entry.
- **Expanded Advanced Debug catalog:** PS5 Firmware Spoof, NP Fake Sign-in,
  NanoDNS, Chukei DNS, zFTPd/zHTTP, and Garlic Save Manager, with
  component-specific availability and firmware checks.
- **Legacy engine selector:** PS5HEN or HyperCore on the exact supported
  3.xx/4.xx entries. The declared 1.xx/2.xx entries retain PS5HEN only;
  HyperCore is unavailable below 3.00.
- **Separate 5.xx engine choices:** Bypervisor for the dedicated hypervisor
  session, or HyperCore for the compatibility stack. The Bypervisor choice
  skips HyperCore, A53/PPR, and BackPork, then continues to Automount and
  Dashboard services. The HyperCore choice follows its compatibility path
  directly.
- **Legacy Dashboard and Toolbox continuation:** the early firmware routes
  now continue to utility, daemon, Dashboard, and Toolbox services after
  engine readiness and Automount instead of stopping at the legacy stage.
- **HyperDeck Media-icon registration and Dashboard handoff improvements**
  across the declared routes, including the legacy and 7.xx profiles, with
  readiness checks and reuse of an existing owned service instance.
- **Safer WebKit and ShellUI lifecycle handling:** Toolbox startup waits for
  manual browser closure and the two-second quiet interval. ShellUI
  replacement is distinguished from actual standby/resume, improving
  recovery after payload launches from Advanced Debug or HyperDeck.
- **Selectors and additional notifications follow the console language.**
  Startup, readiness, handoff, and legacy-path messages extend the existing
  Toolbox translations. Rendering was checked for **31 system-language
  values using 28 catalogs**, including aliases and fallbacks.
- **13.40/13.42 lite route:** these exact profiles move from utility-only
  mode to Kstuff-lite, restricted Automount-lite, and Dashboard/Toolbox
  services. Backport/fakelib, emulator replacement, and automatic AMPR
  features remain disabled in this route.
- **Dedicated 9.40 and 12.70 startup routing and payload inputs**, with exact
  dispatch instead of borrowing neighboring firmware profiles.
- **ShadowMountPlus 1.7 beta9 integration:** standard and lite Automount
  variants, stronger checks for conflicting live owners, and firmware/engine
  readiness checks before startup. Existing configuration, customized
  notifications, and explicit LAN binding are preserved; the default API
  address remains loopback.
- **Consistent HyperHEN v0.9.3 branding** in welcome notifications,
  Toolbox/About, HyperDeck, and Console Information. The console-information
  label now shows HyperHEN instead of the inherited OnionHEN name while
  retaining the firmware string.
- **Dedicated experimental 13.60 Linux-loader input**, kept separate from
  the existing common clean-boot loader. This extends the existing Linux
  handoff; it does not establish hardware certification for that profile.

## Firmware profiles and startup order

Firmware detection is exact. Only the entries below are declared profiles;
nearby versions are never assumed compatible. The total of **67 profiles
already existed in v0.9.2**. This release updates routing, engine choices,
and service continuation rather than claiming 67 newly supported firmwares.

<!-- PROFILE_TABLE_BEGIN -->
| Route | Count | Exact firmware versions | Startup behavior |
| --- | ---: | --- | --- |
| Legacy selector | 25 | 1.00, 1.01, 1.02, 1.05†, 1.10†, 1.11†, 1.12, 1.13†, 1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00, 3.10, 3.20, 3.21, 4.00, 4.02†, 4.03, 4.50, 4.51 | PS5HEN; HyperCore choice enabled only on the exact 3.xx/4.xx entries. Automount and Toolbox follow the selected engine's readiness checks. |
| 5.xx selector | 4 | 5.00, 5.02, 5.10, 5.50 | Bypervisor or HyperCore, then Automount and Toolbox after readiness. |
| A53/PPR | 25 | 6.00, 6.02, 6.50, 7.00, 7.01, 7.20, 7.40, 7.60, 7.61, 8.00, 8.20, 8.40, 8.60, 9.00, 9.05, 9.20, 9.60, 10.00, 10.01, 10.20, 10.40, 10.60, 11.00, 11.20, 11.40 | A53/PPR preparation and HyperCore, then Automount and Toolbox. |
| Dedicated 9.40 | 1 | 9.40 | Dedicated hypervisor and HyperCore inputs, then Automount and Toolbox. |
| HyperCore without PPR | 6 | 11.60, 12.00, 12.02, 12.20, 12.40, 12.60 | HyperCore with PPR-dependent behavior disabled, then Automount and Toolbox. |
| Hypervisor route | 4 | 12.70, 13.00, 13.20, 13.60 | Firmware-specific hypervisor stage, HyperCore, Automount and Toolbox. |
| Kstuff-lite | 2 | 13.40, 13.42 | HyperCore lite and restricted Automount lite, then Toolbox. |
<!-- PROFILE_TABLE_END -->

† **Limited on-demand ShellCore hooks:** 1.05, 1.10, 1.11, 1.13, and 4.02
retain the scanner/API path where supported; full on-demand hooks are not
claimed. The experimental Linux-loader inputs have their own exact-profile
gates and do not make every firmware in this table a Linux profile.

The Toolbox startup correction underlying this release was confirmed working
by the reported **9.05 tester**. Local validation covers the exact route
tables, startup gates, version metadata, notification rendering, and embedded
components. Source coverage does not mean that every firmware has completed
hardware testing.

## Integrated services

The existing kernel-log server on TCP **3232**, FTP server on TCP **2121**,
and debug LAN ELF loader on TCP **9025** remain available alongside the
expanded Advanced Debug catalog. The private loader on TCP **9020** remains
separate from the external first-hop loader on **9021**.

Plugin management, title-targeted SPRX modules, cheats, the performance
display, Remote Play pairing, HyperDPI package services, Linux clean-boot
handoff, and user-payload auto-start remain part of the stack. These existing
features are retained rather than presented as new additions in v0.9.3.

HyperHEN still requires an already jailbroken console and an external
[elfldr](https://github.com/ps5-payload-dev/elfldr) endpoint on TCP **9021**
for the first hop. Follow any firmware-specific startup notification before
continuing; the supported 13.00/13.20/13.60 hypervisor route requires its
Rest Mode/resume completion step.

## Updating and first launch

1. Perform a full reboot before loading the new ELF so the previous ShellUI
   module is replaced.
2. Launch **HyperHEN-v0.9.3.elf**, choose an engine where offered, and follow
   the firmware-specific notifications.
3. **Close WebKit manually and return to the PS5 dashboard.** Allow the
   continuous two-second quiet interval, then open **Settings → HyperHEN
   Toolbox** once startup completes.

If reporting a problem, include the exact firmware, the ELF version, and a
klog from the same boot.

## Credits and special thanks

HyperHEN integration, firmware orchestration, product identity, and release
engineering: **MICHELE MEDIA**. Every upstream component retains its original
authorship, copyright notices, and license terms. See the
[complete README credits](https://github.com/Michele-M-Media/HYPERHEN#credits)
and [third-party notices](https://github.com/Michele-M-Media/HYPERHEN/blob/main/THIRD_PARTY_NOTICES.md)
for the developers and projects behind the integrated tools and services.

The added native-tool integrations credit their original developers:

- [PS5 Firmware Spoof](https://github.com/illusionyy/ps5-fw-spoof) — **illusionyy**.
- [NP Fake Sign-in](https://github.com/earthonion/np-fake-signin) and
  [Garlic SaveMgr](https://github.com/earthonion/garlic-savemgr) — **earthonion**.
- [nanoDNS](https://github.com/drakmor/nanoDNS) and
  [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) — **drakmor and contributors**.
- [Chukei DNS](https://github.com/Al-Azif/chukei-dns) — **Al Azif (Al-Azif)**.
- [zFTPd + zHTTP](https://github.com/seregonwar/zftpd) — **Seregon (SeregonWar)**.
- [PS5 Game Compressor](https://github.com/juma-sayeh/PS5-Game-Compressor) — **Juma Sayeh**.
- [PS5 Web File Manager](https://github.com/owendswang/ps5-web-file-manager) — **owendswang**.
- [CheatRunner](https://github.com/notmaj0r/CheatRunner) — **notmaj0r (maj0r)**.
- [PS5Upload](https://github.com/phantomptr/ps5upload) — **PhantomPtr**.
- [websrv and the PKG installation helper](https://github.com/ps5-payload-dev/websrv) —
  **John Törnblom and the ps5-payload-dev contributors**.

Special thanks to [**@oG_LuckyXmods**](https://x.com/oG_LuckyXmods) for the
extensive help testing HyperHEN, and to the
[**PizzaClub testers**](https://t.me/+6SY-QLIzDkI1Mzg0) for their time,
support, and feedback throughout testing.

## One more thing

### HYPERHEN START.
