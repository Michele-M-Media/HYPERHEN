# HyperHEN startup voice

`hyperhen_start.pcm` is an original synthetic voice sting created for
HyperHEN. It contains no sampled game or console audio.

The dry speech was rendered with eSpeak NG 1.52.0, then mixed and normalized
with FFmpeg into headerless signed 16-bit little-endian stereo PCM at 48 kHz.
It is embedded into the bootstrapper at build time and is never loaded from an
external path on the console.
