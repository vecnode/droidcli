# droidcli

![C++](https://img.shields.io/badge/language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white) ![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-blue.svg)

`droidcli` is a library to create agents that execute multimodal tasks, a headless CLI agent daemon.

Core capabilities: 
- Interactive terminal dashboard [FTXUI](https://github.com/ArthurSonzogni/FTXUI), default mode - chat panel plus live connector/task/log views
- [Ollama](https://ollama.com/) tool-calling agent loop, self-contained (no MCP client): the model can act on this machine directly
- Persistent, per-session agent memory (SQLite-backed) - conversations survive a restart and are queryable over the HTTP API
- Pluggable LLM provider abstraction: [Ollama](https://ollama.com/)
- Filesystem tools (read/write/list/stat files, resolve executables on PATH)
- Run shell commands, and open/launch GUI applications (detached, not blocked on)
- Installed-application discovery, scanned at startup (Windows Add/Remove Programs registry, plus built-in Windows accessories)
- Generic connectors (http_peer or launched_process, PID-tracked) and a persistent task queue
- Structured JSONL application log, with session and background-thread attribution
- Bearer-token authentication on the whole HTTP API by default
- Media decode [FFmpeg](https://www.ffmpeg.org/)

Full design notes: [ARCHITECTURE.md](./ARCHITECTURE.md).
 Working in the repo as an agent: [AGENTS.md](./AGENTS.md).

## Philosophy

- Human user ownership is the foundational constraint.
- Security-first, with escape hatches follows from local-first.
- Minimal keeps binary size, dependencies, and surface area small.
- Provider-agnostic keeps the agent pluggable.

No telemetry. No cloud tenancy. No license server.

## Build

Requires CMake 3.20+ and Git. [FFmpeg](https://www.ffmpeg.org/) is downloaded automatically on first configure.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure   # optional
```

Run it: `.\build\Release\droidcli.exe` (Windows, MSVC generator) or `./build/droidcli` (Linux).

Windows shortcut: `.\build_and_run.bat` (`Debug`/`Release`, `--configure`, `--clean`, `--no-run`).

# License

Licensed under the [Apache 2.0](./LICENSE) license.