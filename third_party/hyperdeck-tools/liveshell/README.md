<p align="center">
  <img src="assets/icon0.png" width="140" alt="LiveShell Engine">
</p>

<h1 align="center">LiveShell Engine</h1>

<p align="center"><b>Animated video wallpapers for the PS5 home screen.</b></p>

<p align="center">
  <img src="https://img.shields.io/badge/version-1.3-white?style=flat-square" alt="version">
  <img src="https://img.shields.io/badge/platform-PS5-white?style=flat-square" alt="platform">
  <img src="https://img.shields.io/badge/video-H.264%20%7C%20HEVC%20%E2%89%A41080p-white?style=flat-square" alt="video">
  <img src="https://img.shields.io/badge/license-GPL--3.0-white?style=flat-square" alt="license">
</p>

---

## Features

- **Discover**: hundreds of animated wallpapers to download right on the PS5, by category (Anime, Games, Movies, ...) the list updates by itself.
- **Library** tab with all your games and apps, to give each one its own video (experimental).
- **PS Store only**: the video shows only on the PlayStation Store tile, with its own video.
- **Sound**: videos with sound play it.
- **Game music**: a game's own theme keeps playing when its video has no sound or Sound is on Mute
- **Your clips** from internal storage or a USB drive
- **Looks**: Dim, Vignette, Haze, all combinable
- **Hide in Settings**: Settings keeps the normal PS5 background
- **Pause while a game runs**, so games get the video decoder
- **Parallax** effect when moving between games
- **Random video** on every start
- **Rest mode**: comes back on its own after waking the PS5
- **Home-screen app** to pick, delete and control everything
- **Phone / PC page** to upload, play and delete videos

## Install

Needs a jailbroken PS5 with a payload loader.

1. Copy `.mp4` files to `/data/LiveShell/video`, or to a `liveshell` folder on a USB drive.
2. Load `liveshell.elf`.
3. Open the **LiveShell Engine** on the home screen (Media).

Videos: H.264 and HEVC Only 1080p. with or without sound and 4k not supported yet.

## Discover

1. Open the **Discover** tab.
3. Download and Press **Use**: it plays on the home screen or per game. Downloaded wallpapers also appear in **All** tab.

## A video for a game (experimental)

1. Open the **Library** tab and pick a game or app.
2. Choose its video. **Reset** gives it back the default background.

Keep all videos in one place (internal **or** USB) for the quickest switching.

**A video for every tile**

- 🎮 **Games**: PS5 and PS4
- 🧩 **Homebrew and web-app tiles**: Payload Manager, etaHEN Toolbox, and more
- 🛍️ **PlayStation Store**: with PS Store only.

## Folders on the PS5

| Path | What |
|---|---|
| `/data/LiveShell/video/` | Your videos |
| `<USB>/liveshell/` | Videos on a USB drive |
| `/data/LiveShell/config.txt` | Settings |
| `/data/LiveShell/liveshell.log` | Log |

## Building

Linux or WSL, with the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) and the [OnionHEN](https://github.com/aydencharles/onionHEN) v0.9.0-beta2 source tree.

```sh
ONIONHEN=~/onionHEN-beta2 PS5_PAYLOAD_SDK=~/ps5-payload-sdk tools/build.sh
```

`src/` is copied into OnionHEN as `source/liveshell` and built as the `liveshell` target.

## Credits

- **[OnionHEN](https://github.com/aydencharles/onionHEN/)**: payload injector, detour library and ShellUI groundwork
- **[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)**: toolchain and PS5 libraries

## License

GPL-3.0. See [LICENSE](LICENSE).
