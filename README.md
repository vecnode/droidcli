# droidcli

![C++](https://img.shields.io/badge/language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white) ![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-blue.svg)

`droidcli` is a personal desktop assistant and CLI.

Core capabilities: 
- Interactive terminal dashboard [FTXUI](https://github.com/ArthurSonzogni/FTXUI), default mode - chat panel plus live connector/task/log views
- [Ollama](https://ollama.com/) tool-calling agent loop, self-contained (no MCP client): the model can act on this machine directly
- Reliability-first agent loop (Classify → Execute → Phrase): the model gets exactly one decision per turn and never narrates its own outcome - execution is always deterministic code, and every mutating tool independently re-verifies its own effect against real, observable state before the model's told it succeeded
- Persistent, per-session agent memory (SQLite-backed): conversation history, a "lessons learned" store of past command fixes, and a name→path memory for known locations - survives restarts, queryable over the HTTP API
- Provider-agnostic LLM provider with OpenAI: Using [Ollama](https://ollama.com/)
- Filesystem tools (read/write/list/stat files, resolve executables on PATH), plus OS clipboard read/write with read-back verification
- Run shell commands, and resolve/launch GUI applications and built-in Windows panels through a trust-ordered, fully verified resolution chain
- Installed-application and Windows-location discovery (Add/Remove Programs registry, known folders, Settings/Control Panel deep links), scanned at startup
- Generic connectors (`http_peer` or `launched_process`, PID-tracked) and a persistent task queue with one-shot and cron-style recurring scheduling
- Structured JSONL application log, with session and background-thread attribution
- Bearer-token authentication on the whole HTTP API by default, secrets (API token) DPAPI-encrypted at rest on Windows
- Self-health watchdog
- Media decode [FFmpeg](https://www.ffmpeg.org/)

Target includes thinking models, we test locally with [GLM-4.7-Flash](https://ollama.com/library/glm-4.7-flash).

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