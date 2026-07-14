# AGENTS.md — working in the metaagent repo

Guidance for AI coding agents (and humans) working in this repository. Keep
changes consistent with the conventions below. This file is the canonical agent
guide; `CLAUDE.md` defers to it.

## What this repository is

**droidcli** is the **C++ agent controller and network trigger**, built as a
portable C++17 control library (`src/`, namespace `droidcli::`) plus the
**droidcli** headless CLI agent daemon (`cli/`, entrypoint
`cli/droidcli.cpp`). There is no windowed desktop app anymore — `app/`
(WebView2/GTK) was deleted entirely. droidcli is a process you start once; it
keeps a persistent task queue and dispatches queued tasks to whatever peers
are registered as **connectors**.

> **Full internal rename.** The C++ namespace (`droidcli::`), the umbrella
> library files (`droidcli.h`/`droidcli.cpp`), the export macro
> (`DROIDCLI_API`), and env var prefixes (`DROIDCLI_*`) all match the
> `droidcli` product name. The repository directory (`metaagent/`) and the
> GitHub repo name are unchanged — only internal code identifiers moved.

Versioning: the library is at **0.2.0** — release 0.2.x builds freely, but
do **not** bump to 0.3 without the owner's say-so.

**No more hardcoded peers.** Where 0.2.0 hardcoded two specific peer apps (a
LoRA adapter inference service and an openFrameworks media player) behind
`/api/adapter/*` and `/api/media/*`, droidcli instead has a generic
**connector** concept (`src/net/connector.hpp`, `net::Connector` /
`net::ConnectorRegistry`): a connector is either an `http_peer` (reached by
URL, proxied via `/api/connectors/{id}/call`) or a `launched_process` (a
local command droidcli can launch/stop and PID-track via `ProcessManager`,
through `/api/connectors/{id}/launch|stop`). Connectors are registered from a
`--config connectors.json` file or at runtime via `POST /api/connectors` —
core has zero compiled-in knowledge of any specific peer. Work for a
connector can be queued as a `Task` (`src/app/tasks.hpp`,
`app::TaskQueue`) and droidcli's daemon loop drains it via
`DroidHost::tick_tasks()`.

> **Ollama stays separate.** `ai::LanguageAiRuntime` / `/ai/chat` is an
> ancillary general **text-generation** endpoint (`--ollama-url`, default
> `:11434`) — it is not a connector, it's built into the core AI seam. Any
> purpose-trained inference service (the old LoRA adapter, or anything else)
> is registered as an ordinary `http_peer` connector instead, with no
> special-cased code path.

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
droidcli.h / droidcli.cpp   Umbrella public API; single TU includes all module .cpp
src/
  core/        Vec3, math, log_sink, value types
  media/       PNG/JPEG decode, probe, MediaStore
  net/         Router + handlers (inbound), connector (generic peer registry), json
  notify/      Notify body parsing
  session/     RuntimeSession + status strings
  app/         Command registry, runtime catalog, tasks (persistent task queue)
  ai/          Ollama text-gen client + LanguageAiRuntime
  runtime/     Host service callbacks (recording/AI)
cli/           droidcli host: DroidHost (config store, ConnectorRegistry +
               TaskQueue, Ollama wiring), ProcessManager (PID-tracked launch
               of any launched_process connector), HTTP route mount
               (CustomRouteFn), droidcli.cpp entrypoint
tools/         mini_http_server (raw-socket HTTP server, custom-route
               fallback hook) + sync_http_client (outbound HTTP/HTTPS)
tests/         One *_test.cpp per core module (no engine, no network)
config/        connectors.example.json (generic, illustrative connector config)
cmake/         FFmpeg.cmake (auto-download helper)
third_party/   Vendored deps (FFmpeg is local-only, git-ignored)
distribute/    Templates staged into the dist (run_all.bat, README_DIST.txt)
```

Public include is `#include "droidcli.h"`. Everything compiles through the
single `droidcli.cpp` translation unit — **a new `src/<module>/foo.cpp` must be
`#include`d from `droidcli.cpp`** or it will not be built into the library.
`cli/*.cpp` and `tools/*.cpp` are NOT part of that TU — they're separate
translation units linked into the `droidcli` executable target.

## Build, run, test

All commands from the repo root. CMake 3.20+. Internet on first configure
(FFmpeg auto-download). There is no windowed app anymore, so no WebView2/GTK
runtime is required to build or run anything in this repo.

```sh
# Library + tests only (fast inner loop; same on Windows/Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# droidcli (Windows, MSVC x64)
build_and_run.bat            # Debug/Release, --configure, --clean, --no-run

# droidcli (Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j && ./build/droidcli

# Portable distribution (droidcli.exe + FFmpeg DLLs, dist/ folder + zip)
build_and_distribute.bat   # see README "Distribution"
```

**Distribution rules:** the whole tree builds with **one MSVC runtime**
(`CMAKE_MSVC_RUNTIME_LIBRARY` in the root CMakeLists: dynamic Debug, static
Release) — never set a per-target runtime that diverges, the Release link breaks.
droidcli does **not** auto-discover peer apps by path convention — connectors
are explicit config (`connectors.json`/`--config`); keep
`build_and_distribute.bat` staging and `distribute/run_all.bat` in sync if you
change that contract. `.bat` files must be **CRLF**.

**Always run `ctest` after touching anything in `src/`.** Tests are
engine-free and network-free by design; if a change makes a core module require a
real socket, GPU, or file, the change is in the wrong layer.

## Conventions

- **C++17**, four-space-tab indentation matching the surrounding file. Allman
  braces (opening brace on its own line) as in existing `.cpp` files.
- **No framework-type leak in core public types.** Use `core::String`,
  `core::Array<T>`, `core::Vec3` — never raw `std::vector` in a public core header.
- **Namespaces** mirror folders: `droidcli::net`, `droidcli::ai`,
  `droidcli::app`, `droidcli::cli` (droidcli host only, not portable).
- **Export macro:** annotate public free functions with `DROIDCLI_API` (see
  `export.hpp`); inline class methods don't need it.
- **Host I/O via `std::function` callbacks**, never direct calls into a
  transport library from core.
- Keep JSON building/parsing in `net/json` + the module that owns the shape; the
  host should not hand-roll core JSON.

## Adding things (follow the existing extension points)

These mirror `ARCHITECTURE.md` → "Extension points". Touch all the listed sites
in one change so the core/host/test trio stays in sync:

1. **HTTP route (inbound)** — handler in `net/handlers.cpp`, register in the
   router; mount it in `cli/http_mount.cpp`'s `CustomRouteFn`.
2. **Validated command** — `CommandId` + `validate_command` in `app/commands`,
   a host-side handler in `apply_command_side_effects`.
3. **Ollama text-gen** — change request/response shaping in `ai/ollama_client` +
   `ai/language_runtime`; the host owns the actual POST via
   `LanguageAiTransportCallbacks`. Do not bake a specific model/endpoint into core.
4. **New connector (peer app)** — usually **config-only**, not a code change.
   For an `http_peer`, add an entry to `connectors.json` (or
   `POST /api/connectors`) with a `base_url`; `DroidHost::call_connector`
   proxies to it generically via `/api/connectors/{id}/call`. For a
   `launched_process`, `ProcessManager` already generalizes over any
   `launch_cmd`/`work_dir` (see `cli/process_manager.{hpp,cpp}`) — only touch
   `DroidHost::launch_connector`/`stop_connector` in `cli/host.cpp` if the
   process needs bespoke lifecycle behavior beyond launch/stop.
5. **New task command** — `app::Task.command` is dispatched in
   `DroidHost::tick_tasks()` (`cli/host.cpp`): `"launch"`/`"stop"` map to
   `launch_connector`/`stop_connector`, anything else is treated as an HTTP
   path called on the task's `connector_id` via `call_connector`. Extend that
   `if`/`else if` chain for a new dispatch kind.
6. **Outbound HTTPS to an external (non-localhost) API** — `tools/sync_http_client`
   routes `https://` URLs through WinHTTP (Windows-native, no new dependency)
   while `http://` keeps the original raw-socket path. Any `https://` connector
   or external integration gets this transport for free — just build the URL.
   Don't add a second, parallel HTTPS implementation.

**Every new core `src/<module>/*_test.cpp` must be registered in
`CMakeLists.txt`** and pass under `ctest`.

## Guardrails

- Don't commit build trees or vendored binaries: `build/`, `build-msvc/`, `dist/`,
  `third_party/ffmpeg/`, `*.exe`, `*.dll`, `*.lib` are all git-ignored — keep it
  that way.
- Don't hardcode secrets, ports, or peer URLs/paths in core. Peer endpoints are
  connector config (`connectors.json` / `POST /api/connectors`), never baked
  into `src/`.
- Don't add a parallel command table or JSON schema in a host — the core is the
  single source of truth; hosts only bridge I/O.
- Don't reintroduce engine/UE modules or scoping (removed at 0.2.0).
- Don't reintroduce a windowed app (WebView2/GTK) or hardcode a specific peer
  (adapter/media-player) back into core or the connector dispatch path —
  peers are config, not code.
- Don't break the host seams (`HostServiceCallbacks`, `RouteTable`,
  `SignalTransportFn`, `ConnectorRegistry`, `TaskQueue`) without updating
  `ARCHITECTURE.md`.

## Host configuration

**droidcli** (`cli/droidcli.cpp`) is configured by CLI flags: `--port`
(default `30080`), `--config <path>` (connectors JSON file, loaded via
`net::parse_connector_from_json` per array entry), `--no-ai`, `--ollama-url`,
`--ollama-model`, `--daemon` (documented no-op — always runs in the
foreground; use a process supervisor for true background operation), and
`--headless` (skip the default FTXUI TUI, run the plain foreground
daemon+HTTP-API loop only). The Ollama URL/model are runtime-editable via
`POST /api/config` / `POST /api/ollama/config`.

**Centralised process control** lives in `cli/process_manager.{hpp,cpp}`
(Windows Job Object / POSIX process group, so stop kills the whole tree; bare
command names are made `.\`-relative when they exist in the working dir because
of `NoDefaultCurrentDirectoryInExePath`). `DroidHost::launch_connector` /
`stop_connector` (mounted at `/api/connectors/{id}/launch|stop`,
`/api/process/status`) launch `launched_process` connectors and report their
PIDs, keyed by connector id (not a hardcoded process name). Commands and work
dirs come from the connector's `launch_cmd`/`work_dir` fields — never
hardcode a user's paths in core.

There is no auto-start-on-init anymore: droidcli only launches a
`launched_process` connector when told to, via `POST
/api/connectors/{id}/launch` or a queued `{"command":"launch"}` task. (The
old windowed app auto-launched both hardcoded peers on startup; that
behavior was peer-specific and did not generalize, so it was dropped rather
than ported — an operator who wants auto-start can queue a launch task from
their own startup script instead.)

Product/UI controls and HTTP route tables live in `README.md`; deep design and
layer model live in `ARCHITECTURE.md`. Keep those two in sync when you change
behavior.
