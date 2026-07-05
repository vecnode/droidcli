# AGENTS.md — working in the metaagent repo

Guidance for AI coding agents (and humans) working in this repository. Keep
changes consistent with the conventions below. This file is the canonical agent
guide; `CLAUDE.md` defers to it.

## What this repository is

`metaagent` is the **C++ agent controller and network trigger** for a
three-application system. It is a portable C++17 control library plus a desktop
host (`app/`, window title **agent core 0.2.0**) and a headless server
(`tools/`). It does not render or play media itself — it coordinates the other
two applications over HTTP.

Versioning: the library/app are at **0.2.0** — release 0.2.x builds freely, but
do **not** bump to 0.3 without the owner's say-so.

| # | Application | Role | This repo's link to it |
| - | ----------- | ---- | ---------------------- |
| 1 | **metaagent** (this repo) | Agent controller + network trigger: command + signal dispatch, corpus reading, HTTP in/out, process control | — |
| 2 | **Adapter inference** (LoRA) | The trained **LLaVA 1.5 LoRA** adapter — OCR-text → summary generation *only*. Already working; FastAPI service in its own repo ([vecnode/pre-training](https://github.com/vecnode/pre-training), local at `C:\Users\luisarandas\Desktop\UFO_FILES`) | Desktop host proxy: `/api/adapter/status`, `/api/adapter/summarize` → `METAAGENT_ADAPTER_URL` (default `http://127.0.0.1:8008`, `POST /api/summarize`) |
| 3 | **media-player-cpp** (openFrameworks) | Plays the media that metaagent coordinates (clips, subtitles, focus crops) | Reached via the host media proxy (`/api/media/*` → `METAAGENT_MEDIA_PLAYER_URL`, default `http://127.0.0.1:8080`) |

> **Two separate AI models — keep them apart.** **Ollama** (`METAAGENT_OLLAMA_URL`,
> `:11434`) is an *ancillary* general **text-generation** endpoint behind
> `ai::LanguageAiRuntime` / `/ai/chat` (the *Agent* panel + subtitle condenser) —
> it is **not** one of the three apps. The **LoRA adapter** (app #2, `:8008`) is
> the purpose-trained model used only for its OCR→summary generation (the
> *Document Adapter* panel). They have different endpoints, different host code
> paths, and different UI panels. Never route adapter work through the Ollama seam.

> **No engine code.** Unreal Engine / particle / camera support was removed from
> this repo entirely (0.2.0). Do not reintroduce engine-scoped modules, UE
> callbacks, or "ue5" runtime scoping.

## Golden rule: what belongs in core vs the host

> If it touches a real socket, process, window/WebView, GPU, or runtime
> filesystem/network **transport**, it stays in a host (`app/` or `tools/`). If
> it is pure state + parsing + JSON + validation, it belongs in `src/` (core).

Core never links httplib, FFmpeg transport, WebView2, or GTK directly — hosts
inject those through callbacks (`LanguageAiTransportCallbacks`,
`SignalTransportFn`, `HostServiceCallbacks`, `HandlerContext`). When you add a
feature, decide which side of this line it falls on **before** writing code. A
new transport is a host concern; a new message shape or validation rule is a
core concern.

## Repository map

```
metaagent.h / metaagent.cpp   Umbrella public API; single TU includes all module .cpp
src/
  core/        Vec3, math, log_sink, value types
  media/       PNG/JPEG decode, probe, MediaStore, corpus (OCR/objects/summaries)
  net/         Router + handlers (inbound), signal_router (triggers), json
  notify/      Notify body parsing
  session/     RuntimeSession + status strings
  app/         Command registry, runtime catalog
  ai/          Ollama text-gen client + LanguageAiRuntime (not the LoRA adapter)
  runtime/     Host service callbacks (recording/AI)
app/           Desktop host: WebView + embedded httplib server (MetaAgentHost),
               endpoints/config store, media/adapter proxies, dataset reader,
               ProcessManager (PID-tracked launch of the peer apps)
tools/         Headless metaagent_server CLI + mini_http_server
tests/         One *_test.cpp per core module (no engine, no network)
cmake/         FFmpeg.cmake (auto-download helper)
third_party/   Vendored deps (FFmpeg is local-only, git-ignored)
external/      Submodules: pre-training (app #2) + media-player-cpp (app #3)
distribute/    Templates staged into the dist (run_all.bat, README_DIST.txt)
```

Public include is `#include "metaagent.h"`. Everything compiles through the
single `metaagent.cpp` translation unit — **a new `src/<module>/foo.cpp` must be
`#include`d from `metaagent.cpp`** or it will not be built into the library.

## Build, run, test

All commands from the repo root. CMake 3.20+. Internet on first configure when
building the app (FetchContent + FFmpeg auto-download).

```sh
# Library + tests only (fast inner loop; same on Windows/Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# Desktop app (Windows, MSVC x64, needs WebView2 Runtime)
build_and_run.bat            # Debug/Release, --configure, --clean, --no-run

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
- **No framework-type leak in core public types.** Use `core::String`,
  `core::Array<T>`, `core::Vec3` — never raw `std::vector` in a public core header.
- **Namespaces** mirror folders: `metaagent::net`, `metaagent::ai`,
  `metaagent::app`, `metaagent::app_host` (desktop host only).
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
   a host-side handler in `apply_command_side_effects`.
4. **Ollama text-gen** — change request/response shaping in `ai/ollama_client` +
   `ai/language_runtime`; the host owns the actual POST via
   `LanguageAiTransportCallbacks`. Do not bake a specific model/endpoint into core.
5. **LoRA adapter integration** — this is a *separate* seam from Ollama. The
   desktop host proxies it directly (`MetaAgentHost::build_adapter_status_json` /
   `proxy_adapter_summarize` in `app/src/metaagent_host.cpp`, mounted at
   `/api/adapter/*`). Keep it out of the `ai/` text-gen path; add fields to
   `HostConfig::adapter_url` + `update_config` + the Settings Endpoints table.
6. **New controlled process** — a `ProcessManager` key + host method + route +
   UI button (see `/api/media/build` as the template).

**Every new core `src/<module>/*_test.cpp` must be registered in
`CMakeLists.txt`** and pass under `ctest`.

## Guardrails

- Don't commit build trees or vendored binaries: `build/`, `build-msvc/`, `dist/`,
  `third_party/ffmpeg/`, `*.exe`, `*.dll`, `*.lib` are all git-ignored — keep it
  that way.
- Don't hardcode secrets, ports, or peer URLs/paths in core. Ports/URLs/dirs for
  the media player and the adapter are host configuration (env vars below).
- Don't add a parallel command table or JSON schema in a host — the core is the
  single source of truth; hosts only bridge I/O.
- Don't reintroduce engine/UE modules or scoping (removed at 0.2.0).
- Don't break the host seams (`HostServiceCallbacks`, `RouteTable`,
  `SignalTransportFn`) without updating `ARCHITECTURE.md`.

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
| `METAAGENT_AUTOSTART_MEDIA_PLAYER` | on (default-on flag) | `0` disables auto-launching the media player on host init |
| `METAAGENT_AUTOSTART_ADAPTER` | on (default-on flag) | `0` disables auto-launching the adapter server on host init |

**Centralised process control** lives in `app/src/process_manager.{hpp,cpp}`
(Windows Job Object / POSIX process group, so stop kills the whole tree; bare
command names are made `.\`-relative when they exist in the working dir because
of `NoDefaultCurrentDirectoryInExePath`). The host
(`MetaAgentHost::build_media_player` / `run_media_player` /
`launch_adapter_server` / `stop_*`, mounted at `/api/media/build|run`,
`/api/media/process/stop`, `/api/adapter/launch`, `/api/adapter/process/stop`,
`/api/process/status`) launches the peer apps and reports their PIDs. Commands
and project dirs are configuration — never hardcode a user's paths in core.

**Auto-start on host init:** `MetaAgentHost::initialize()` calls `run_media_player()`
and `launch_adapter_server()` (launch only, never build) right after setup,
gated by `HostConfig::auto_start_media_player` / `auto_start_adapter` (both
default `true`, read via `env_flag_enabled_default_on()` in `main.cpp` — note
this is the inverse-default sibling of `env_flag_enabled()`: unset/`1` = on,
only `0`/`false`/`no` turns it off). Silently no-ops if the matching
`*_project_dir` is empty. Runs after `initialize()`'s lock scope releases,
since `run_media_player`/`launch_adapter_server` take the lock themselves.

All URLs/model/paths are also editable live in the app's **Settings → Endpoints**
table, which `POST`s to `/api/config` (`MetaAgentHost::update_config`) and
overrides the env var for the running session.

The **headless server** (`tools/metaagent_server.cpp`) is configured by CLI
flags instead: `--port` (default `30080`), `--ollama-url`, `--ollama-model`,
`--no-ai`.

Product/UI controls and HTTP route tables live in `README.md`; deep design and
layer model live in `ARCHITECTURE.md`. Keep those two in sync when you change
behavior.
