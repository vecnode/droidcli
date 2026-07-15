# droidcli

![C++](https://img.shields.io/badge/language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white)  
![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-blue.svg)

`droidcli` is a library to create agents that execute multimodal tasks, a headless CLI agent daemon.

Core capabilities: 
- HTTP (inbound + outbound)
- media decode (FFmpeg-backed)
- Ollama API seam (incl. tool-calling)
- Persistent task queue
- Generic connectors (http_peer or launched_process, PID-tracked)

Full design notes: [ARCHITECTURE.md](./ARCHITECTURE.md).
 Working in the repo as an agent: [AGENTS.md](./AGENTS.md).

## Build

Requires CMake 3.20+ and Git.

**Windows** - VS 2022 **MSVC** x64

On first configure, FFmpeg is downloaded automatically into `third_party/ffmpeg/` when missing.

```powershell
cmake -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Debug -j
.\build-msvc\Debug\droidcli.exe
```

Shortcut: `.\build_and_run.bat` (configures only when needed; accepts `Debug`/`Release`, `--configure`, `--clean`, `--no-run`).

Release: use `--config Release` → `build-msvc\Release\droidcli.exe`

**Linux** - C++20

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/droidcli
```

### Library + tests only

**Windows / Linux** (same commands):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```


# License

Licensed under the [Apache 2.0](./LICENSE)  license.