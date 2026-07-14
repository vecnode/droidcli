# CLAUDE.md

Guidance for Claude Code in this repository. The full agent/contributor guide is
in `AGENTS.md` — read it first.

@AGENTS.md

## Claude-specific quick reference

- **`droidcli` is the C++ agent controller / network trigger** (library
  version **0.2.0** — hold at 0.2.x, do not bump to 0.3). The portable core
  (`src/`, namespace `droidcli::`) is used by **droidcli** (`cli/`, entrypoint
  `cli/droidcli.cpp`), a headless CLI agent daemon — no windowed app exists
  anymore (`app/` was deleted). droidcli talks to peers through a generic
  **connector** concept (`net::Connector`: `http_peer` or `launched_process`),
  config-driven via `--config connectors.json` or `POST /api/connectors` —
  there is no compiled-in knowledge of any specific peer app.
- **Full internal rename to droidcli** — the C++ namespace (`droidcli::`),
  umbrella library files (`droidcli.h`/`droidcli.cpp`), export macro
  (`DROIDCLI_API`), CMake targets, test binaries, and `DROIDCLI_*` env vars
  all match the product name now. The CMake **library target** is
  `droidcli_core` (the `droidcli` name is taken by the CLI executable
  target). The repository directory/GitHub repo name (`metaagent`) is
  unchanged.
- **Ollama stays separate from connectors.** `ai::LanguageAiRuntime`/`/ai/chat`
  (`--ollama-url`, default `:11434`) is the ancillary text-gen seam, built into
  core — it is not a connector. Any inference service (the old LoRA adapter
  included) is just an ordinary `http_peer` connector now, reached via
  `/api/connectors/{id}/call`.
- **Persistent task queue.** `app::TaskQueue` (`src/app/tasks.hpp`) holds
  pending/running/done/failed `Task`s; `DroidHost::tick_tasks()`
  (`cli/host.cpp`) drains one per poll-loop iteration, dispatching to
  `launch_connector`/`stop_connector`/`call_connector` depending on the task's
  `command`. Routes: `POST/GET /api/tasks`, `GET /api/tasks/{id}`.
- **No engine code.** Unreal Engine / particle / camera support was removed at
  0.2.0 — do not reintroduce engine modules, UE callbacks, or "ue5" scoping.
- **HTTPS exists now, but only for external APIs.** `tools/sync_http_client`
  routes `https://` URLs through WinHTTP while `http://` still uses the
  original raw-socket path for local peers. Don't add a second HTTPS
  implementation. If a connector or feature needs a secret (API key, token),
  never echo it back via `GET /api/config` — only a `*_configured: bool`.
- **Before editing, decide core vs host.** Pure state/parsing/JSON/validation →
  `src/`. Any real transport, process, window, or filesystem I/O → a host via a
  callback. See the "Golden rule" in `AGENTS.md`.
- **Fast inner loop:**
  ```sh
  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
  ctest --test-dir build --output-on-failure
  ```
  Run `ctest` after every change under `src/`. Tests are engine-free and
  network-free — keep them that way. On Windows prefer `build_and_run.bat`
  (MSVC); a stray MinGW on PATH breaks the FFmpeg link in fresh trees (also
  breaks `winhttp.h` resolution for `droidcli`/`tools/sync_http_client.cpp` -
  build with the MSVC generator, e.g. `cmake -B build -G "Visual Studio 17
  2022" -A x64`, not a MinGW toolchain).
- **A new `src/<module>/foo.cpp` must be `#include`d from `droidcli.cpp`**, and
  a new `*_test.cpp` must be registered in `CMakeLists.txt`. `cli/*.cpp` is
  separate — those are droidcli host translation units, not part of the core
  library TU.
- **Don't** add a parallel command/JSON schema in a host, hardcode peer URLs
  or paths in core (peers are connector config, not code), reintroduce a
  windowed app, or commit build trees / `dist/` / vendored binaries (all
  git-ignored).
- Product UI + HTTP route tables: `README.md`. Layer model + extension points:
  `ARCHITECTURE.md`. Keep both in sync with behavior changes.
