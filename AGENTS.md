# AGENTS.md — working in the metaagent repo

Guidance for AI coding agents (and humans) working in this repository. Keep
changes consistent with the conventions below. This file is the canonical agent
guide; `CLAUDE.md` defers to it.

## Current focus

droidcli's Core-tier feature set (provider abstraction, persistent memory,
structured logging, task scheduling, a self-health watchdog, a read-only
hardware inventory) is largely in place — see "Current status and next
hardening priorities" in `ARCHITECTURE.md` for the up-to-date read and the
ranked list of what's next (a real background service, a recurring
scheduler, config hardening, then the Edge/Channel tier). The single largest
area of active work historically was **agent-turn reliability**: the local
models droidcli runs against are small and tool-calling-tuned but
frequently unreliable, and the old multi-hop `run_agent_tool_loop` design
carried a growing set of guards to catch it fabricating success claims,
leaking model output, or falsely denying capability. That design was
replaced by **Classify -> Execute -> Phrase** (`DroidHost::agent_turn` -
`classify_via_llm`/`execute_decision_or_pause`/`finish_turn_after_execution`/
`phrase_result`, `cli/host.cpp`): the model decides at most one action per
turn, via exactly one classification call, and never narrates its own
outcome - execution is always deterministic code, and the reply is either a
zero-LLM-call template or a second, narrowly grounded phrasing call. Most of
the old narration-policing guards became unnecessary rather than relocated,
since there's no free-form narration left for them to police. See "The
agent turn" in `ARCHITECTURE.md` for the current flowchart, and extension
point 7 below before adding a deterministic bypass around the classifier.

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
> `droidcli` product name. The local repository directory (`metaagent/`) is
> unchanged; the GitHub repo itself has been renamed to `vecnode/droidcli`.

Versioning: **droidcli 0.1.0** is the first release under this name — a full
rewrite of the earlier `metaagent` windowed app (0.2.x) into a headless CLI
agent daemon, so versioning restarts rather than continuing that lineage.

**No more hardcoded peers.** Where the old app hardcoded two specific peer apps (a
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

> **Ollama stays separate.** `ai::LanguageRuntime` / `/ai/chat` is an
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
inject those through callbacks (`LanguageTransportCallbacks`,
`HandlerContext`). When you add a feature, decide which side of this line it
falls on **before** writing code. A new transport is a host concern; a new
message shape or validation rule is a core concern.

## Repository map

```
droidcli_core.h / droidcli_core.cpp   Umbrella public API; single TU includes all module .cpp
src/
  core/        Vec3, math, log_sink, value types
  media/       PNG/JPEG decode, probe, MediaStore
  net/         Router + handlers (inbound), connector (generic peer registry), json
  notify/      Notify body parsing
  session/     RuntimeSession + status strings
  app/         tasks (persistent task queue)
  ai/          Ollama text-gen client (incl. tool-calling) + LanguageRuntime
cli/           droidcli host: DroidHost (config store, ConnectorRegistry +
               TaskQueue with one-shot scheduling, Ollama wiring, the
               agent_turn tool-calling/reliability loop - see "The agent
               turn" in ARCHITECTURE.md for its control-flow diagram),
               MemoryStore (SQLite session history + command-fix lessons),
               a throttled self-health watchdog, hardware_info (opt-in local
               CPU/GPU/RAM/disk inventory), ProcessManager (PID-tracked
               launch of any launched_process connector), command_runner
               (one-shot shell exec), HTTP route mount (CustomRouteFn),
               droidcli.cpp entrypoint (incl. bearer token resolution)
tools/         mini_http_server (raw-socket HTTP server, bearer-token check,
               custom-route fallback hook) + sync_http_client (outbound HTTP/HTTPS)
tests/         One *_test.cpp per core module (no engine, no network)
config/        connectors.example.json (generic, illustrative connector config)
cmake/         FFmpeg.cmake (auto-download helper)
third_party/   Vendored deps (FFmpeg is local-only, git-ignored)
distribute/    Templates staged into the dist (run_all.bat, README_DIST.txt)
db/            Runtime databases/state (SQLite memory, connector state, last
               TUI session) - all git-ignored except db/README.md, see there
```

Public include is `#include "droidcli_core.h"`. Everything compiles through the
single `droidcli_core.cpp` translation unit — **a new `src/<module>/foo.cpp` must be
`#include`d from `droidcli_core.cpp`** or it will not be built into the library.
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
- **Every agent-tool JSON result carries `"ok"`, as the first field, always.**
  This is the model's only trustworthy success signal (see "Phase 6" in
  `ARCHITECTURE.md` for the incident that made this a hard rule, not a
  style preference) — never assume a caller will infer success from an
  exit code, a `"launched"` flag, or the presence of output.
- **A command-execution tool prone to repeated model mistakes** (wrong
  filter syntax, a misremembered flag) should point the model at
  `search_command_fixes`/`record_command_fix` (`cli/host.cpp`, see "Phase
  8" in `ARCHITECTURE.md`) in its own tool description — droidcli's
  persistent, model-driven memory of past command bugs and their fixes,
  backed by a `command_lessons` table in the same `MemoryStore` database
  conversation history already uses. Don't build a second, tool-specific
  memory mechanism for this.

## Adding things (follow the existing extension points)

These mirror `ARCHITECTURE.md` → "Extension points". Touch all the listed sites
in one change so the core/host/test trio stays in sync:

1. **HTTP route (inbound)** — handler in `net/handlers.cpp`, register in the
   router; mount it in `cli/http_mount.cpp`'s `CustomRouteFn`. Every `/api/*`
   route (and `/ai/chat`) is gated by the bearer-token check in
   `tools::MiniHttpServer::poll_once` before any dispatch runs — no per-route
   auth code needed.
2. **Ollama text-gen** — change request/response shaping in `ai/ollama_client` +
   `ai/language_runtime`; the host owns the actual POST via
   `LanguageTransportCallbacks`. Do not bake a specific model/endpoint into core.
   Tool-calling (`ToolDefinition`/`ToolCall`, `"tools"` request field,
   `message.tool_calls` response parsing) lives in `ai/ollama_client` too, but
   the multi-hop loop that executes tool calls is host-side
   (`DroidHost::agent_turn` in `cli/host.cpp`) since it calls back into
   connector/task/process methods that aren't portable.
3. **New connector (peer app)** — usually **config-only**, not a code change.
   For an `http_peer`, add an entry to `connectors.json` (or
   `POST /api/connectors`) with a `base_url`; `DroidHost::call_connector`
   proxies to it generically via `/api/connectors/{id}/call`. For a
   `launched_process`, `ProcessManager` already generalizes over any
   `launch_cmd`/`work_dir` (see `cli/process_manager.{hpp,cpp}`) — only touch
   `DroidHost::launch_connector`/`stop_connector` in `cli/host.cpp` if the
   process needs bespoke lifecycle behavior beyond launch/stop.
4. **New task command** — `app::Task.command` is dispatched in
   `DroidHost::tick_tasks()` (`cli/host.cpp`): `"launch"`/`"stop"` map to
   `launch_connector`/`stop_connector`, `"run"` maps to
   `cli::run_command_once` (see `cli/command_runner.{hpp,cpp}`), anything else
   is treated as an HTTP path called on the task's `connector_id` via
   `call_connector`. Extend that `if`/`else if` chain for a new dispatch kind,
   and set `Task::result_json` if the new command produces more than a bare
   success flag.
5. **Outbound HTTPS to an external (non-localhost) API** — `tools/sync_http_client`
   routes `https://` URLs through WinHTTP (Windows-native, no new dependency)
   while `http://` keeps the original raw-socket path. Any `https://` connector
   or external integration gets this transport for free — just build the URL.
   Don't add a second, parallel HTTPS implementation.
6. **New agent tool** — add to `DroidHost::agent_tool_definitions()` (name,
   description, JSON Schema parameters) and a matching branch in
   `DroidHost::execute_agent_tool()` (`cli/host.cpp`). Tools call back into
   `DroidHost`'s own methods (connectors/tasks/run_command) — never a second,
   parallel command dispatch table. Two rules that came out of a real
   hallucination incident (see "Phase 6" in `ARCHITECTURE.md`), both
   required for every new tool:
   - **Every tool result must have an `"ok"` boolean, first field.** The
     model has shown it will not reliably infer success from anything
     else (a `"launched":true` flag, a numeric exit code, the mere presence
     of output) — `"ok"` is the only field it's conditioned to trust. If the
     tool wraps `CommandRunResult`, use `command_succeeded()`
     (`cli/command_runner.hpp`) rather than re-deriving the check inline —
     two independent copies of that logic already drifted apart once.
   - **If the tool has a side effect** (writes to disk, runs a process,
     touches a connector/task/filesystem beyond a read), add its name to
     `tool_call_requires_approval()` (`cli/host.cpp`) so it pauses for human
     approval via `POST /api/agent/tool_decision` instead of executing the
     instant the model asks for it. Read-only tools should NOT be added
     there — gating them only slows the agent down for no safety benefit.
   - **If the tool shells out and never needs shell features** (pipes,
     redirects, env var expansion — `run_ffmpeg` is the precedent, see
     "Phase 7" in `ARCHITECTURE.md`), call `run_command_once(...,
     via_shell=false)` on Windows. Routing a command through `cmd.exe /c`
     re-tokenizes the whole string with `cmd.exe`'s own grammar before the
     target program ever sees it, which silently corrupts an argument
     containing embedded double quotes (a filter expression, a JSON blob,
     …) — `CreateProcess`'s own command-line parsing does not have this
     problem and is what the target program's own `argv` parsing expects
     anyway.
   - **If the model has been observed reliably making the same mistake
     despite the right fact already being in its system prompt** (see
     `desktop_path`/`substitute_bare_desktop_token()` in "Phase 7",
     `ARCHITECTURE.md` — the model kept writing a bare `desktop/...` path
     even after being told the real one), a deterministic substitution at
     the tool-execution layer is a reasonable fix once the pattern is
     reproducible, not a one-off guess. Report what actually ran back to
     the model (a `"resolved_args"`/`"resolved_command"`-style field) so it
     can tell the user the truth instead of what it originally typed.
   - **If the tool creates or acts on a file/artifact and the caller gives no
     location at all** (a bare filename, or `run_command`/`run_ffmpeg` with
     no `work_dir`), default it to the user's real Desktop, not droidcli's
     own working directory — droidcli is a personal desktop assistant, not a
     dev/build tool (see "Agent properties" in `ARCHITECTURE.md`), and a
     location-less reference should land somewhere a human would actually go
     looking for it. `default_bare_filename_to_desktop()` (`cli/host.cpp`,
     "Phase 20") is the existing helper — reuse it rather than writing a
     second copy. Anything with *any* directory information at all, even a
     relative one, is left untouched as the caller having already specified
     where.
   - **If the tool can fail in a way the model has enough information to fix
     itself** (a bad path, a bad argument — anything where the failure
     reason points at a concrete correction, not an external/permanent
     condition), add its name to the `is_retriable_tool` list checked in
     `DroidHost::finish_turn_after_execution` (`cli/host.cpp`) so a failure
     triggers the one bounded auto-retry (the real error is appended to the
     transcript and `classify_via_llm` is called once more) instead of just
     being reported and stopping there.
7. **Don't add a deterministic recognizer that bypasses the LLM classifier.**
   An earlier design had a `src/intent/` module (`open_intent`,
   `create_file_intent`, `create_image_intent`, `pending_command`) that
   pattern-matched narrow, high-confidence request shapes with pure string
   scanning ahead of `classify_via_llm`, plus a standalone TUI/HTTP
   quick-open flow (`try_quick_open_json`) built on the same recognizer. It
   was removed: `classify_via_llm`'s one LLM classification call is now the
   sole source of every `TurnDecision`, for every request shape, no
   exceptions. If a specific phrasing proves unreliable, fix it through the
   tool description or the model/provider choice, not by adding a second,
   parallel routing path that only some requests go through.

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
- Don't break the host seams (`RouteTable`, `ConnectorRegistry`, `TaskQueue`)
  without updating `ARCHITECTURE.md`.
- Don't add a new `/api/*` (or `/ai/chat`) route that bypasses the bearer-token
  check — it's centralized in `tools::MiniHttpServer::poll_once`
  (`request_requires_auth`/`is_authorized` in `tools/mini_http_server.cpp`);
  every route under those paths gets it automatically, so there should never
  be a reason to add a second, route-local auth check.
- Don't let `DroidHost::agent_turn`'s tool loop call into anything that isn't
  already a `DroidHost` method with its own validation — tools are a second
  entry point into the same surface `/api/*` exposes, not a way around it.
- **droidcli does not consume MCP servers.** Following ZeroClaw's
  self-contained/minimal philosophy (https://docs.zeroclawlabs.ai), agent
  tools are native `DroidHost` methods (see `filesystem_tools.{hpp,cpp}` and
  `command_runner.{hpp,cpp}` for the pattern) — droidcli executes capabilities
  itself rather than shelling out to external MCP servers as an MCP client.
  If MCP support is ever added, it's droidcli exposing *itself* as an MCP
  server (so external MCP clients can drive it), not the other way around —
  don't add an MCP client dependency chain.
- **Don't launch a process from an unresolved bare name.** Every process
  launch (`open_application`, `run_command`, `run_ffmpeg`) must resolve to a
  real, verified path through the fixed trust order in "Windows execution
  ruleset" (`ARCHITECTURE.md`) before `CreateProcess` is ever called — never
  let the OS's own unverified bare-name search (cwd/system dirs/PATH) run
  before droidcli's own curated sources (App Paths registry, the
  installed-apps index, `list_windows_locations`) get a chance. If nothing
  in that order resolves, fail outright with a clear error — never fall
  through to a blind, unverified attempt in the hope something coincidental
  on PATH happens to match. This section is meant to be a stable contract:
  a change to the resolution order is rare and deliberate, and updates
  `ARCHITECTURE.md`'s "Windows execution ruleset" section in the same
  commit, not code that drifts out of sync with it.

## Host configuration

**droidcli** (`cli/droidcli.cpp`) is configured by CLI flags: `--port`
(default `30080`), `--config <path>` (connectors JSON file, loaded via
`net::parse_connector_from_json` per array entry), `--no-ai`, `--ollama-url`,
`--ollama-model`, `--token <value>` (bearer token for `/api/*` + `/ai/chat`;
falls back to `DROIDCLI_API_TOKEN`, then a generated-and-printed random
token — see README.md "Security"), `--daemon` (documented no-op — always runs
in the foreground; use a process supervisor for true background operation),
and `--headless` (skip the default FTXUI TUI, run the plain foreground
daemon+HTTP-API loop only). `--help`/`-h` prints usage and exits; `--version`/`-v`
prints the version and exits - both short-circuit before any host/server
setup. The Ollama URL/model are runtime-editable via `POST /api/config` /
`POST /api/ollama/config`.

**State persistence**: runtime-registered connectors (via `POST
/api/connectors` or the agent, not `--config`) are saved to
`db/droidcli_state.json` (git-ignored, see `db/README.md`) on clean exit and
reloaded on the next start, before `--config` is applied - `--config` entries
win over a stale saved entry with the same id (`register_connector` replaces
by id). Only a graceful exit (SIGINT/SIGTERM, or the TUI's `q`/Ctrl+C) reaches
the save call at the end of `main()`; a forced kill loses unsaved state, same
as any other daemon.

**Durable logging**: every `append_app_log()` call (the same one behind
`/api/app/log`) also writes to `logs/log.txt` (created automatically,
git-ignored except `logs/README.md`), with a full date+time timestamp and a
`=== droidcli session started ===` marker per run - unlike the in-memory,
capped, per-session `app_log_`, this persists across restarts and crashes for
after-the-fact debugging.

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
