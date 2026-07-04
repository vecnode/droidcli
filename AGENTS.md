# AGENTS.md — working in the metaagent repo

Guidance for AI coding agents (and humans) working in this repository. Keep
changes consistent with the conventions below. This file is the canonical agent
guide; `CLAUDE.md` defers to it.

## What this repository is

`metaagent` is the **C++ agent controller and network trigger** for a
three-application system. It is a portable C++17 domain library plus a desktop
host (`app/`) and a headless server (`tools/`). It does not render or play media
itself — it coordinates the other two applications over HTTP.

| # | Application | Role | This repo's link to it |
| - | ----------- | ---- | ---------------------- |
| 1 | **metaagent** (this repo) | Agent controller + network trigger: particle/camera/media domain logic, command + signal dispatch, HTTP in/out | — |
| 2 | **Adapter inference** (LoRA) | The trained **LLaVA 1.5 LoRA** adapter — OCR-text → summary generation *only*. Already working; FastAPI service in its own repo ([vecnode/pre-training](https://github.com/vecnode/pre-training), local at `C:\Users\luisarandas\Desktop\UFO_FILES`) | Desktop host proxy: `/api/adapter/status`, `/api/adapter/summarize` → `METAAGENT_ADAPTER_URL` (default `http://127.0.0.1:8008`, `POST /api/summarize`) |
| 3 | **media-player-cpp** (openFrameworks) | Plays the media that metaagent coordinates (clips, subtitles, focus crops) | Reached via the host media proxy (`/api/media/*` → `METAAGENT_MEDIA_PLAYER_URL`, default `http://127.0.0.1:8080`) |

> **Two separate AI models — keep them apart.** **Ollama** (`METAAGENT_OLLAMA_URL`,
> `:11434`) is an *ancillary* general **text-generation** endpoint behind
> `ai::LanguageAiRuntime` / `/ai/chat` (the *Text Assistant* panel) — it is **not**
> one of the three apps. The **LoRA adapter** (app #2, `:8008`) is the
> purpose-trained model used only for its OCR→summary generation (the *Document
> Adapter* panel). They have different endpoints, different host code paths, and
> different UI panels. Never route adapter work through the Ollama seam.

The fourth consumer is an **Unreal Engine 5 plugin** that embeds the same core
library unchanged (see `ARCHITECTURE.md` → "UE plugin split"). The plugin source
is **not** in this repo; do not invent it, but keep the host-callback seams it
depends on stable.

Treat these four embeddings as the contract: the same portable core must satisfy
the desktop app, the headless server, the UE plugin, and the inference/media
peers it talks to.

## Golden rule: what belongs in core vs the host

> If it touches Epic/Niagara APIs, a real GPU/viewport, a window/WebView, or
> runtime filesystem/network **transport**, it stays in a host (`app/`,
> `tools/`, or the UE plugin). If it is pure state + math + JSON + validation, it
> belongs in `src/` (core).

Core never links httplib, FFmpeg, WebView2, or GTK directly — hosts inject those
through callbacks (`SchedulerCallbacks`, `ParticleHostCallbacks`,
`LanguageAiTransportCallbacks`, `SignalTransportFn`, `HandlerContext`). When you
add a feature, decide which side of this line it falls on **before** writing
code. A new transport is a host concern; a new message shape, validation rule, or
state machine edge is a core concern.

## Repository map

```
metaagent.h / metaagent.cpp   Umbrella public API; single TU includes all module .cpp
src/
  core/        Vec3, math, log_sink, value types (no FVector/FString)
  media/       PNG/JPEG decode, MediaStore, mask pipeline, corpus (PDF_TEXT/OBJS_TEXT)
  camera/      Zoom, cinematic rig + CameraController
  particle/    Pattern FSM, scheduler, actuation, shapes/mask, visual continuity, effects
  net/         Router + handlers (inbound), platform_client (outbound), signal_router (triggers), json
  notify/      Notify body parsing
  session/     RuntimeSession + status strings
  app/         Command registry, GUI catalog, action validation, runtime catalog
  runtime/     Host service + ParticleHostCallbacks
  input/       Input policy (GUI-open vs observation mode)
app/           Desktop host: WebView + embedded httplib server (MetaAgentHost)
tools/         Headless metaagent_server CLI + mini_http_server
tests/         One *_test.cpp per core module (no engine, no network)
cmake/         FFmpeg.cmake (auto-download helper)
third_party/   Vendored deps (FFmpeg is local-only, git-ignored)
external/      Submodules: pre-training (app #2) + media-player-cpp (app #3)
distribute/    Templates staged into the dist (run_all.bat, README_DIST.txt)
```

Public include is `#include "metaagent.h"` (or `<metaagent/metaagent.h>` when
vendored). Everything compiles through the single `metaagent.cpp` translation
unit — **a new `src/<module>/foo.cpp` must be `#include`d from `metaagent.cpp`**
or it will not be built into the library/aggregate.

## Build, run, test

All commands from the repo root. CMake 3.20+. Internet on first configure when
building the app (FetchContent + FFmpeg auto-download).

```sh
# Library + tests only (fast inner loop; same on Windows/Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# Desktop app (Windows, MSVC x64, needs WebView2 Runtime)
cmake -B build-msvc -G "Visual Studio 17 2022" -A x64 -DMETAAGENT_BUILD_APP=ON
cmake --build build-msvc --config Debug -j
./build-msvc/app/Debug/metaagent-app.exe        # or .\app\build_and_run.bat

# Desktop app (Linux, needs GTK3 + WebKit2GTK dev packages)
cmake -B build -DMETAAGENT_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j && ./build/metaagent-app  # or ./app/build_and_run.sh

# Portable distribution (Release build of everything + dist/ folder + zip)
build_and_distribute.bat   # see README "Distribution"; needs MSYS2 for the media player
```

**Distribution rules:** the whole tree builds with **one MSVC runtime**
(`CMAKE_MSVC_RUNTIME_LIBRARY` in the root CMakeLists: dynamic Debug, static
Release) — never set a per-target runtime that diverges, the Release link breaks.
The app auto-discovers the dist layout (sibling `media-player/`, `adapter/deploy/`,
`datasets/` next to the exe — `apply_dist_layout_defaults` in `app/src/main.cpp`);
keep that in sync with `build_and_distribute.bat` staging. Weights and media are
git-ignored in the submodules — the distribute script copies them from configured
working copies, never from `external/`. `.bat` files must be **CRLF**.

**Always run `ctest` after touching anything in `src/`.** Tests are
engine-free and network-free by design; if a change makes a core module require a
real socket, GPU, or file, the change is in the wrong layer.

## Conventions

- **C++17**, four-space-tab indentation matching the surrounding file. Allman
  braces (opening brace on its own line) as in existing `.cpp` files.
- **No engine/STL-leak in core public types.** Use `core::String`,
  `core::Array<T>`, `core::Vec3` — never `FString`, `FVector`, or raw
  `std::vector` in a public core header.
- **Namespaces** mirror folders: `metaagent::particle`, `metaagent::net`,
  `metaagent::ai`, `metaagent::app`, `metaagent::app_host` (desktop host only).
- **Export macro:** annotate public free functions with `METAAGENT_API` (see
  `export.hpp`); inline class methods don't need it.
- **Host I/O via `std::function` callbacks**, never direct calls into a
  transport library from core.
- Keep JSON building/parsing in `net/json` + the module that owns the shape; the
  host should not hand-roll core JSON.

## Adding things (follow the existing extension points)

These mirror `ARCHITECTURE.md` → "Extension points". Touch all the listed sites
in one change so the core/host/test trio stays in sync:

1. **HTTP route (inbound)** — handler in `net/handlers.cpp`, register in the
   router; mount it in the host (`app/src/metaagent_http_mount.cpp` and/or
   `tools/metaagent_server.cpp`).
2. **Signal/trigger (the "network trigger" path)** — extend
   `net/signal_types` (envelope/target shape + JSON) and `net/signal_router`
   (dispatch/log); the host supplies the `SignalTransportFn`. Add a
   `signal_router_test` case.
3. **Validated command** — `CommandId` + `validate_command` in `app/commands`,
   a host-side handler, and optionally a GUI action ID in `app/gui_actions`.
4. **FSM transition** — row in `transition_graph.cpp` + branch in
   `apply_visual_continuity_for_transition()` + assertions in
   `transition_graph_test` and `visual_continuity_test`.
5. **Forming mode / camera style / shape source** — see the numbered list in
   `ARCHITECTURE.md`; always mirror any enum the UE TypeBridge depends on.
6. **Ollama text-gen** — change request/response shaping in `ai/ollama_client` +
   `ai/language_runtime`; the host owns the actual POST via
   `LanguageAiTransportCallbacks`. Do not bake a specific model/endpoint into core.
7. **LoRA adapter integration** — this is a *separate* seam from Ollama. The
   desktop host proxies it directly (`MetaAgentHost::build_adapter_status_json` /
   `proxy_adapter_summarize` in `app/src/metaagent_host.cpp`, mounted at
   `/api/adapter/*`). Keep it out of the `ai/` text-gen path; add fields to
   `HostConfig::adapter_url` + `update_config` + the Settings Endpoints table.

**Every new core `src/<module>/*_test.cpp` must be registered in
`CMakeLists.txt`** and pass under `ctest`.

## Guardrails

- Don't commit build trees or vendored binaries: `build/`, `build-msvc/`,
  `third_party/ffmpeg/`, `*.exe`, `*.dll`, `*.lib` are all git-ignored — keep it
  that way.
- Don't hardcode secrets, ports, or peer URLs in core. Ports/URLs for the media
  player and the adapter endpoint are host configuration (env vars below).
- Don't add a parallel FSM, command table, or JSON schema in a host — the core
  is the single source of truth; hosts only bridge I/O.
- Don't break the UE seams (`ParticleHostCallbacks`, `HostServiceCallbacks`,
  `RouteTable`, `SignalTransportFn`) without updating `ARCHITECTURE.md`.

## Host configuration

The **desktop app** (`app/src/main.cpp`) reads env vars:

| Variable | Default | Purpose |
| -------- | ------- | ------- |
| `METAAGENT_NO_AI` | off | `1` disables `/ai/chat` (Ollama text-gen) |
| `METAAGENT_OLLAMA_URL` | `http://127.0.0.1:11434` | Ollama **text-gen** base URL (ancillary, not app #2) |
| `METAAGENT_OLLAMA_MODEL` | `llama3.2` | Ollama text-gen model name |
| `METAAGENT_SYSTEM_PROMPT` | built-in | System prompt for Ollama text-gen turns |
| `METAAGENT_ADAPTER_URL` | `http://127.0.0.1:8008` | **LoRA adapter** inference base URL (app #2) |
| `METAAGENT_MEDIA_PLAYER_URL` | `http://127.0.0.1:8080` | media-player-cpp base URL (app #3) |
| `METAAGENT_MEDIA_DATA_DIR` | empty | Local media dataset dir (corpus + clip mirror fallback) |
| `METAAGENT_MEDIA_PLAYER_DIR` | empty | media-player-cpp project dir (build/run) |
| `METAAGENT_MEDIA_BUILD_CMD` | `make Release` | Media player build command (MSYS2 MinGW64) |
| `METAAGENT_MEDIA_RUN_CMD` | `media-player-cpp.exe` | Media player run binary (launched in project `bin/`) |
| `METAAGENT_ADAPTER_DIR` | empty | pre-training `deploy/` dir (uv server) |
| `METAAGENT_ADAPTER_LAUNCH_CMD` | `deploy.bat` | Adapter server launch command |
| `METAAGENT_DATASET_DIR` | empty | pre-training `output/` dir; corpus CSVs read by `GET /api/dataset` |

**Centralised process control** lives in `app/src/process_manager.{hpp,cpp}`
(Windows Job Object / POSIX process group, so stop kills the whole tree). The
host (`MetaAgentHost::build_media_player` / `run_media_player` /
`launch_adapter_server` / `stop_*`, mounted at `/api/media/build|run`,
`/api/media/process/stop`, `/api/adapter/launch`, `/api/adapter/process/stop`,
`/api/process/status`) launches the peer apps and reports their PIDs. Commands
and project dirs are configuration — never hardcode a user's paths in core.

All four URLs/model are also editable live in the app's **Settings → Endpoints**
table, which `POST`s to `/api/config` (`MetaAgentHost::update_config`) and
overrides the env var for the running session.

The **headless server** (`tools/metaagent_server.cpp`) is configured by CLI
flags instead: `--port` (default `30080`), `--ollama-url`, `--ollama-model`,
`--no-ai`.

Outbound forwarding to a UE/orchestrator host is implemented in core
(`net/platform_client`, `PlatformEndpointConfig`, default event endpoint
`/api/unreal/event`) but is **not currently wired to an env var in the shipped
hosts** — wire it through a host if you need it, and update this table.

Product/UI controls and HTTP route tables live in `README.md`; deep design,
layer model, and the UE plugin split live in `ARCHITECTURE.md`. Keep those two in
sync when you change behavior.
