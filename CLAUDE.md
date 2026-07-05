# CLAUDE.md

Guidance for Claude Code in this repository. The full agent/contributor guide is
in `AGENTS.md` — read it first.

@AGENTS.md

## Claude-specific quick reference

- **`metaagent` is the C++ agent controller / network trigger** (app title
  **agent core 0.2.0** — hold at 0.2.x, do not bump to 0.3). The portable core
  (`src/`) is shared by the desktop app (`app/`) and the headless server
  (`tools/`), and it talks to two peer apps — the **LoRA adapter inference**
  service (LLaVA OCR→summary, its own `/api/adapter/*` proxy) and the
  **media-player-cpp** openFrameworks player (via `/api/media/*`).
- **Two separate AI models — don't conflate.** The **LoRA adapter** (app #2,
  `METAAGENT_ADAPTER_URL` :8008) is proxied directly by the desktop host
  (`/api/adapter/*`, *Document Adapter* panel). **Ollama** (`METAAGENT_OLLAMA_URL`
  :11434) is an ancillary text-gen endpoint behind the `ai/` seam (`/ai/chat`,
  *Agent* panel + subtitle condensing) — not one of the three apps.
- **No engine code.** Unreal Engine / particle / camera support was removed at
  0.2.0 — do not reintroduce engine modules, UE callbacks, or "ue5" scoping.
- **HTTPS exists now, but only for external APIs.** `tools/sync_http_client`
  routes `https://` URLs through WinHTTP (added for Google's Custom Search JSON
  API in `src/net/google_search.*`) while `http://` still uses the original
  raw-socket path for the local peers. Don't add a second HTTPS implementation.
  Never echo the Google API key back via `GET /api/config` — only a
  `*_configured: bool`.
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
  (MSVC); a stray MinGW on PATH breaks the FFmpeg link in fresh trees.
- **A new `src/<module>/foo.cpp` must be `#include`d from `metaagent.cpp`**, and
  a new `*_test.cpp` must be registered in `CMakeLists.txt`.
- **Don't** add a parallel command/JSON schema in a host, hardcode peer URLs
  or paths in core, or commit build trees / `dist/` / vendored binaries (all
  git-ignored).
- Product UI + HTTP route tables: `README.md`. Layer model + extension points:
  `ARCHITECTURE.md`. Keep both in sync with behavior changes.
