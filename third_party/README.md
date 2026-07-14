# Third-party binaries (not in git)

## FFmpeg

Droidcli uses **FFmpeg** (LGPL 2.1+) for media probe and decode. For commercial software, use **dynamic linking** (DLLs on Windows) — do not statically link GPL-enabled FFmpeg builds.

### Automatic setup (Windows)

When CMake configures and FFmpeg is missing, it automatically downloads a shared FFmpeg package and extracts it under `third_party/ffmpeg/`.

Config knobs:

- `DROIDCLI_FFMPEG_AUTO_DOWNLOAD=ON|OFF` (default `ON`)
- `DROIDCLI_FFMPEG_URL=<archive-url>`
- `DROIDCLI_FFMPEG_ROOT=<existing-prefix>`
- `DROIDCLI_FFMPEG_ALLOW_INSECURE_DOWNLOAD=ON|OFF` (default `ON`; retries download with TLS verification disabled when certificate validation fails)

### Layout

Extract a shared/dev FFmpeg build into:

```
third_party/ffmpeg/
  include/     libavcodec/, libavformat/, libswscale/, libavutil/, ...
  lib/         .lib / .a import libraries
  bin/         avcodec-*.dll, avformat-*.dll, ... (Windows runtime)
```

### Windows (MSVC x64)

1. Download a **shared** FFmpeg build with dev files (e.g. [BtbN FFmpeg Builds](https://github.com/BtbN/FFmpeg-Builds/releases) — `ffmpeg-master-latest-win64-gpl-shared.zip` or an **lgpl** shared variant if you need stricter licensing).
2. Copy `include`, `lib`, and `bin` into `third_party/ffmpeg/`.
3. Reconfigure CMake and rebuild.

### Linux

Install dev packages or point `DROIDCLI_FFMPEG_ROOT` at a prefix with `include/` and `lib/`.

This directory is listed in `.gitignore` — nothing under `third_party/ffmpeg/` is pushed to GitHub.
