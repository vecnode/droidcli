# CLAUDE.md

Guidance for Claude Code in this repository. The full agent/contributor guide is
in `AGENTS.md` — read it first.

@AGENTS.md

## Claude-specific quick reference

- **`metaagent` is the C++ agent controller / network trigger** in a four-way
  embedding: the portable core (`src/`) is shared by the desktop app (`app/`),
  the headless server (`tools/`), and an out-of-repo Unreal Engine 5 plugin, and
  it talks to two peer apps — the **LoRA adapter inference** service (LLaVA
  OCR→summary, its own `/api/adapter/*` proxy) and the **media-player-cpp**
  openFrameworks player (via `/api/media/*`).
- **Two separate AI models — don't conflate.** The **LoRA adapter** (app #2,
  `METAAGENT_ADAPTER_URL` :8008) is proxied directly by the desktop host
  (`/api/adapter/*`, *Document Adapter* panel). **Ollama** (`METAAGENT_OLLAMA_URL`
  :11434) is an ancillary text-gen endpoint behind the `ai/` seam (`/ai/chat`,
  *Text Assistant* panel) — not one of the three apps.
- **Before editing, decide core vs host.** Pure state/math/JSON/validation →
  `src/`. Any real transport, GPU, window, or filesystem I/O → a host via a
  callback. See the "Golden rule" in `AGENTS.md`.
- **Fast inner loop:**
  ```sh
  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
  ctest --test-dir build --output-on-failure
  ```
  Run `ctest` after every change under `src/`. Tests are engine-free and
  network-free — keep them that way.
- **A new `src/<module>/foo.cpp` must be `#include`d from `metaagent.cpp`**, and
  a new `*_test.cpp` must be registered in `CMakeLists.txt`.
- **Don't** add a parallel FSM/command/JSON schema in a host, hardcode peer URLs
  or secrets in core, or commit build trees / vendored binaries (all git-ignored).
- Product UI + HTTP route tables: `README.md`. Layer model + UE plugin split +
  extension points: `ARCHITECTURE.md`. Keep both in sync with behavior changes.
