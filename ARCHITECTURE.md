# metaagent - Architecture

Portable C++17 library for MetaAgent **domain logic**: particle pattern mechanics, camera rig math, media/mask + corpus pipeline, HTTP route handlers, signal/trigger dispatch, session snapshots, command validation, and input policy. A host (Unreal, the desktop app, or the headless server) supplies I/O, rendering, transport, and engine APIs through thin bridges.

---

## System context — three cooperating applications

metaagent is the **agent controller and network trigger** in a three-application
system. The portable core decides *what* should happen; hosts and peers perform
the actual transport, inference, and rendering.

```mermaid
flowchart LR
    subgraph MA["1 — metaagent (this repo)"]
        CORE[Portable core src/]
        HOSTS[Hosts: app/ desktop, tools/ headless, UE plugin]
        HOSTS --> CORE
    end
    ADAPTER[("2 — LoRA adapter inference\nLLaVA OCR→summary, FastAPI :8008")]
    MEDIA[("3 — media-player-cpp\nopenFrameworks :8080")]
    OLLAMA[("Ollama — ancillary text-gen :11434")]

    MA -->|host /api/adapter/* → METAAGENT_ADAPTER_URL| ADAPTER
    MA -->|signal_router + media proxy /api/media/*\nMETAAGENT_MEDIA_PLAYER_URL| MEDIA
    MA -->|ai::LanguageAiRuntime → /ai/chat\nMETAAGENT_OLLAMA_URL| OLLAMA
```

| App | What it owns | Seam in this repo |
| --- | ------------ | ----------------- |
| **1. metaagent** | Particle/camera/media domain logic, command + signal dispatch, HTTP in/out | — |
| **2. Adapter inference** (LoRA) | Purpose-trained **LLaVA 1.5 LoRA**; OCR-text → summary generation only ([vecnode/pre-training](https://github.com/vecnode/pre-training), FastAPI) | Desktop host `/api/adapter/status` + `/api/adapter/summarize` proxy to `adapter_url` |
| **3. media-player-cpp** | Playback of clips/subtitles/focus crops | `net::SignalRouter` (triggers) + the desktop host's `/api/media/*` proxy to `media_player_base_url` |

> **Two distinct AI models.** The **LoRA adapter** (app #2) is the trained model,
> used *only* for its OCR→summary generation, surfaced as the *Document Adapter*
> panel. **Ollama** is a separate, ancillary general **text-generation** endpoint
> behind `ai::LanguageAiRuntime` / `/ai/chat` (the *Text Assistant* panel) — it is
> not one of the three apps. All endpoints/models are **configuration**, never
> baked into core.

---

## Design goals


| Goal                   | How                                                                                                                    |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| Portability            | C++17, `metaagent::core::`* value types (no `FVector` / `FString`)                                                     |
| Single source of truth | FSM, solvers, phase curves, actuation compose, scheduler, shape/mask algorithms, camera pose math, HTTP handler bodies |
| Testability            | CMake + unit tests without the editor                                                                                  |
| Engine bridge          | Host converts types and supplies I/O callbacks (Niagara, PNG, world queries, HTTPServer bind, view-target blend)       |


**Rule of thumb:** if it touches Epic APIs, Niagara, the viewport, or runtime filesystem I/O, it stays in the host. If it is pure state + math + JSON + validation, it belongs in core.

---

## Layer model

```mermaid
flowchart TB
    subgraph L0["L0 — Public API"]
        H[metaagent.h]
    end

    subgraph L1["L1 — Domain modules"]
        P[particle: FSM + scheduler + actuation]
        M[media: decode + mask]
        C[camera: rig + controller]
        A[app + session + input]
        N[net + notify]
    end

    subgraph L2["L2 — Core primitives"]
        T[core: Vec3, math, log_sink]
    end

    subgraph L3["L3 — Host adapters (not in this repo)"]
        UE[MetaAgentTypeBridge + Runtime + Control]
    end

    H --> L1 --> L2
    UE --> H
```



### Host integration seams


| Seam                        | Status                                                                                            |
| --------------------------- | ------------------------------------------------------------------------------------------------- |
| **Visual pose**             | **Done** — `DisplayedPose`, `freeze_displayed_pose()`, `apply_visual_continuity_for_transition()` |
| **Particle I/O**            | **Done** — `ParticleHostCallbacks` on `SchedulerCallbacks::particle_host`                         |
| **Shape / mask**            | Core algorithms + UE async cache; host supplies PNG load                                          |
| **Representation delivery** | Core policy + UE driver registry                                                                  |
| **Commands / GUI**          | **Done** — catalog + validation; AI + Recording panel rows                                        |
| **Session snapshot**        | Planned — extend `/health` with particle FSM summary                                              |


---

## Repository layout

```
metaagent/
├── metaagent.h                    Umbrella public API
├── metaagent.cpp                  Single TU — #includes all module .cpp files
├── src/
│   ├── initialize.hpp             initialize_defaults()
│   ├── core/                      Vec3, math, log_sink
│   ├── media/                     PNG/JPEG decode, MediaStore, mask pipeline, corpus (PDF_TEXT/OBJS_TEXT)
│   ├── camera/                    Zoom + cinematic rig/controller
│   ├── particle/                  Pattern domain (FSM, scheduler, actuation, shapes, visual_continuity)
│   ├── net/                       Route table, handlers, platform_client (outbound), signal_router (triggers), json
│   ├── notify/                    Notify body parsing
│   ├── session/                   RuntimeSession + status strings
│   ├── app/                       Command registry, GUI catalog, action validation, runtime catalog
│   ├── ai/                        Ollama text-gen client + LanguageAiRuntime (not the LoRA adapter)
│   ├── runtime/                   Host service + particle host callbacks
│   └── input/                     Input policy (GUI vs gameplay)
├── tools/
│   ├── metaagent_server.cpp       Standalone inbound HTTP CLI
│   └── mini_http_server.cpp       Minimal TCP HTTP server
├── tests/
├── CMakeLists.txt
├── README.md
└── ARCHITECTURE.md
```

Public entry point: `#include <metaagent/metaagent.h>`.

---

## What lives in core vs the UE host


| Area                   | In `metaagent` (portable)                                          | Stays in UE plugin (host)                                         |
| ---------------------- | ------------------------------------------------------------------ | ----------------------------------------------------------------- |
| **Particles**          | FSM, scheduler, actuation, solvers, shape/mask math, state effects | Niagara buffers, direct capture, orchestrator UX, UObject runtime |
| **Camera**             | Orbit pose, sway, zoom, `CameraController`                         | `SetViewTargetWithBlend`, focus queries, observation lock         |
| **HTTP inbound**       | `/health`, `/echo`, `/notify` handlers + router                    | Epic `HTTPServer` bind/listen                                     |
| **HTTP outbound**      | URL/body build + response parse                                    | Async POST transport (`FHttpModule`)                              |
| **Session / commands** | `RuntimeSession`, `validate_command`, `validate_gui_action`        | Key binds, HUD draw, dispatch                                     |
| **Input policy**       | `policy_for_runtime()`                                             | Enhanced Input, mouse hit-test                                    |
| **GUI panel**          | `gui_catalog`, action validation                                   | Canvas draw, click regions                                        |


---

## Particle domain (`metaagent/particle/`)


| Module                     | Key API                                    | Role                                                                               |
| -------------------------- | ------------------------------------------ | ---------------------------------------------------------------------------------- |
| `pattern_types`            | `PatternConfig`, `PatternRuntime`          | FSM state, buffers, curve samples                                                  |
| `transition_graph`         | `TransitionGraph`                          | Declarative FSM table                                                              |
| `scheduler`                | `ParticleScheduler`, `SchedulerCallbacks`  | Tick, transitions, representation frame, **visual continuity via `particle_host`** |
| `visual_continuity`        | `DisplayedPose`, `freeze_displayed_pose()` | Per-edge pose freeze using displayed (on-screen) positions                         |
| `forming_solver`           | `FormingSolverRegistry`                    | Per-particle forming / return motion                                               |
| `actuation_math`           | `ActuationMath`                            | Phase evaluation, position composition                                             |
| `representation_actuation` | `RepresentationActuationPolicy`            | Direct / Parameters / Hybrid delivery                                              |
| `representation_types`     | `RepresentationMapping`                    | Macro phase from pattern state                                                     |
| `shape_builder`            | `ShapeBuilder`                             | Targets, frames, silhouette assignment                                             |
| `image_mask_processor`     | `image_mask::build_mask_from_rgba`         | Mask + stratified scatter                                                          |
| `state_effects`            | `StateEffectStack`                         | Ambient breathing + optional cohesion/turbulence                                   |
| `effect_catalog`           | GUI particle action specs                  | Load preview vs trigger effect ID                                                  |


### Pattern FSM

States: `Idle`, `Preparing`, `Anticipating`, `Forming`, `Holding`, `Returning`, `Dissipating`, `Releasing`.

The scheduler advances via `TransitionGraph::evaluate_transition()`. The UE plugin calls through `MetaAgentParticleCoreBridge` — **no parallel FSM table in the plugin**.

> **Only the UE plugin drives the scheduler.** The desktop app (`app/`) does not
> instantiate or tick a `ParticleScheduler` and implements none of the particle
> callbacks — particles are a UE-only runtime. The `particle` module stays in the
> portable library solely for the plugin to consume; the desktop host links it
> (via the umbrella `metaagent.h`) but never runs it.

#### Manual step mode (`.` key) — current production flow

Silent mask wait; no anticipating noise. Preparing is visually identical to Idle (calm ambient).

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Preparing: step forward (mask loading)
    Idle --> Forming: step forward (mask cached)
    Preparing --> Forming: step forward (mask ready)
    Preparing --> Idle: retreat / cancel
    Forming --> Holding: step forward
    Forming --> Idle: retreat (manual)
    Holding --> Returning: step forward
    Holding --> Forming: retreat / morph
    Returning --> Idle: step forward / timeout
```



#### Auto full-cycle mode (play full reveal)

Uses anticipating motion while the mask loads.

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Anticipating: start / auto advance
    Anticipating --> Forming: mask ready
    Forming --> Holding: form timeout
    Holding --> Returning: hold timeout
    Returning --> Idle: return complete
```



---

## Visual continuity

### Problem

Actuation is a **two-stage pipeline**:

1. **Core compose** — `ActuationMath::compose_world_positions()` blends baseline → targets using phase/state.
2. **Host post-compose** — state-effect offsets (ambient breathing) are added when building the displayed/GPU pose.

Freezing only the composed rest baseline (stage 1) causes a small jump when stage-2 offsets differ across macro phases (e.g. Idle → Preparing).

### Solution (implemented)

```cpp
// particle/visual_continuity.hpp
struct DisplayedPose {
    core::Array<core::Vec3> world_positions;
    core::Vec3 pattern_center;
};

// runtime/host_interfaces.hpp — also on SchedulerCallbacks::particle_host
struct ParticleHostCallbacks {
    std::function<bool(DisplayedPose& out)> read_displayed_positions;
    std::function<void(const core::Array<core::Vec3>&)> apply_world_positions;
    std::function<int32_t()> authoritative_particle_count;
};

void freeze_displayed_pose(ParticleScheduler& scheduler, const DisplayedPose& displayed);
void apply_visual_continuity_for_transition(
    ParticleScheduler& scheduler, const TransitionResult& result, const DisplayedPose& displayed);
```

**Flow on transition:**

1. Scheduler calls `particle_host.read_displayed_positions()` (UE: `LastApplied`, fallback compose).
2. `apply_visual_continuity_for_transition()` sets baseline/targets per edge (Holding, Preparing, Forming, Returning, …).
3. Optional `particle_host.apply_world_positions()` syncs host runtime buffers.
4. `begin_pattern_start` / `enter_pattern_state` sync core → runtime (no duplicate hold logic in UE).

**Tests:** `visual_continuity_test.cpp` — freeze + zero compose delta on Holding, Preparing, Forming, Returning edges.

**Host mitigations retained:** authoritative particle count, Preparing uses Idle macro phase, `pattern_active = false` during Preparing in `build_representation_frame()`.

---

## Camera (`metaagent/camera/`)


| Module       | Role                                                                                          |
| ------------ | --------------------------------------------------------------------------------------------- |
| `types`      | `ZoomSettings`, `CinematicSettings`, `CinematicRuntimeState`, `FocusTarget`, `CinematicStyle` |
| `rig`        | `compute_cinematic_pose`, zoom, focus-from-bounds                                             |
| `controller` | Per-session `CameraController`: enable/disable cinematic, tick pose, zoom                     |


Styles: `OscillatingHold` (sway + hold), `SlowOrbit` (continuous yaw). Cycle via **V** or GUI.

Focus resolution (particle bounds, locked observation target) remains host-side in `FMetaAgentCameraRuntime::ResolveFocusTarget`.

---

## App / session / net / input


| Module                    | Role                                                                  |
| ------------------------- | --------------------------------------------------------------------- |
| `session/types`           | `RuntimeSession`, `FeatureFlags`                                      |
| `app/commands`            | `CommandId`, parse + validate                                         |
| `app/gui_catalog`         | Panel sections, rows, action IDs                                      |
| `app/gui_actions`         | GUI action string IDs → commands                                      |
| `input/policy`            | Block move/look in observation mode; allow wheel zoom when GUI closed |
| `net/router` + `handlers` | `/health`, `/echo`, `/notify`, `/ai/chat`                             |
| `net/platform_client`     | Outbound platform POST build/parse                                    |
| `net/signal_router`       | **Network trigger**: register peer `SignalTarget`s, dispatch `SignalEnvelope`s via `SignalTransportFn`, log delivery |
| `media/corpus`            | Load `PDF_TEXT.md`/`OBJS_TEXT.md`, OCR regions → subtitles, focus crops, region masks |
| `ai/language_runtime`     | Transcript + turn state for **Ollama text-gen** (`/ai/chat`); POST via `LanguageAiTransportCallbacks`. Separate from the LoRA adapter, which the desktop host proxies directly |
| `runtime/host_interfaces` | Recording + AI snapshots/toggles; **ParticleHostCallbacks**           |


---

## UE plugin split


| Plugin path                          | Role                                                                                 |
| ------------------------------------ | ------------------------------------------------------------------------------------ |
| `MetaAgentCoreAggregate.cpp`         | Embeds `metaagent/metaagent.cpp`                                                     |
| `MetaAgentTypeBridge.`*              | UE ↔ core conversion, scheduler bridge, camera sync                                  |
| `MetaAgentParticleRuntime.*`         | UObject instance, Niagara tick glue, **ReadDisplayedPose / ApplyHostWorldPositions** |
| `MetaAgentParticleControl.`*         | Orchestrator, drivers, Niagara profiles                                              |
| `MetaAgentParticleShapes.*`          | PNG load, mask cache, shape providers                                                |
| `MetaAgentPlayerController.*`        | Input, camera host, GUI dispatch, **host service snapshots**                         |
| `Host/MetaAgentHttpBridge.`*         | Inbound HTTP                                                                         |
| `Host/MetaAgentPlatformBridge.*`     | Outbound HTTP                                                                        |
| `Host/MetaAgentHostSession.*`        | Session snapshot                                                                     |
| `Host/MetaAgentInputBridge.*`        | Command / GUI validation                                                             |
| `Host/MetaAgentHostServicesBridge.*` | `HostServiceCallbacks` → recording + AI                                              |


---

## HTTP flow

```mermaid
flowchart LR
    Client[External HTTP client]
    Epic[Epic HTTPServer bind]
    Bridge[FMetaAgentHttpBridge]
    Router[metaagent::net::Router]
    Handlers[handlers.cpp]

    Client --> Epic --> Bridge --> Router --> Handlers
```



Outbound: core `platform_client` builds/parses; `FMetaAgentPlatformBridge` performs async POST.

---

## Network triggers (`metaagent/net/signal_router`)

The "network trigger" half of metaagent's role: a portable registry + dispatcher
for sending typed signals to peer applications (the media player, a UE host, or
any external orchestrator). Core owns the **envelope shape, target registry, and
delivery log**; the host supplies the actual transport.

| Type | Role |
| ---- | ---- |
| `SignalTarget` | Registered peer: `id`, `control_url`, `capabilities`, `enabled` |
| `SignalEnvelope` | Versioned message: `id`, `type`, `target`, `timestamp_ms`, `payload_json` |
| `SignalRouter` | Register/unregister targets, `dispatch(envelope, transport)`, ring-buffered log (128 entries) |
| `SignalTransportFn` | Host-provided `std::function` performing the POST (httplib / `FHttpModule`) |
| `SignalDispatchResult` / `SignalLogEntry` | Per-dispatch outcome + auditable history |

Build/parse helpers (`build_signal_envelope_json`, `parse_signal_envelope`,
`build_targets_json`, `parse_target_from_json`, `build_signal_log_json`) keep all
JSON in core. The same router instance drives media-player cues and any future
peer without a parallel implementation in a host. Tests: `signal_router_test.cpp`.

```mermaid
flowchart LR
    Domain[Core domain event] --> Router[net::SignalRouter]
    Router -->|envelope JSON| Transport[Host SignalTransportFn]
    Transport --> Peer[(Media player / UE / orchestrator)]
    Router --> Log[(Signal log — 128 entries)]
```

---

## Roadmap


| Phase | Goal                                                                  | Status                 |
| ----- | --------------------------------------------------------------------- | ---------------------- |
| A     | Particle FSM + actuation in core                                      | Done                   |
| B     | Inbound HTTP + session/commands in core                               | Done                   |
| C     | Host bridges (Http, HostSession, Input)                               | Done                   |
| D     | Outbound platform client + PlatformBridge                             | Done                   |
| E     | GUI panel catalog + dispatch validation                               | Done                   |
| F     | Camera style registry (`SlowOrbit`)                                   | Done                   |
| G     | Particle effect catalog in core                                       | Done                   |
| H     | Standalone `metaagent_server` CLI                                     | Done                   |
| I     | Recording / AI host_interfaces                                        | Done                   |
| J     | Manual FSM profile (Preparing, no anticipating on `.`)                | Done                   |
| K     | Host-side displayed-pose hold + authoritative count                   | Done (superseded by L) |
| **L** | **Core `DisplayedPose` + `freeze_displayed_pose` + continuity tests** | **Done**               |
| M     | `ParticleHostCallbacks` on scheduler                                  | Done                   |
| N     | Wire recording/AI `HostServiceCallbacks` in UE + GUI rows             | Done                   |
| O     | Authoritative count in core `PatternRuntime`                          | Planned                |
| P     | Headless particle simulator (mock host callbacks) for CI              | Future                 |
| Q     | `SignalRouter` + typed envelopes (network triggers to peer apps)      | Done                   |
| R     | Media corpus (PDF_TEXT/OBJS_TEXT) → subtitles + focus crops          | Done                   |
| S     | Ollama text-gen seam (`LanguageAiRuntime`, `/ai/chat`)               | Done                   |
| T     | Separate LoRA adapter seam (`/api/adapter/*`, LLaVA OCR→summary)     | Done                   |


---

## Build

### Standalone

```powershell
cd metaagent
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests: `transition_graph_test`, `forming_types_test`, `shape_builder_polyline_test`, `actuation_composer_test`, `media_decode_test`, `camera_rig_test`, `net_handler_test`, `signal_router_test`, `app_command_test`, `gui_actions_test`, `platform_client_test`, `gui_catalog_test`, `effect_catalog_test`, `host_interfaces_test`, `language_runtime_test`, `ollama_client_test`, `runtime_catalog_test`, `corpus_test`, `state_effects_test`, `**visual_continuity_test**`.

### Unreal

`MetaAgentCoreAggregate.cpp` includes portable sources; `MetaAgentPlugin.Build.cs` adds `metaagent/src`.

---

## Extension points

1. **New forming mode** — solver in `forming_solver.cpp`, register in `initialize_defaults()`, mirror enum in TypeBridge.
2. **New shape source** — UE provider in shape registry; portable assignment in `shape_builder.cpp`.
3. **New camera style** — `CinematicStyle` + `compute_cinematic_pose` + UE enum/sync.
4. **New HTTP route** — handler in `net/handlers.cpp`, register in router.
4b. **New network trigger / signal type** — extend `net/signal_types` (envelope/target + JSON) and `net/signal_router` (dispatch/log); host supplies the `SignalTransportFn`; add a `signal_router_test` case.
5. **New validated command** — `CommandId` + `validate_command` + host handler + optional GUI action ID.
6. **New FSM transition** — row in `transition_graph.cpp` + case in `apply_visual_continuity_for_transition()` + test in `transition_graph_test.cpp` / `visual_continuity_test.cpp`.
7. **Visual continuity on new edge** — add branch in `apply_visual_continuity_for_transition()` + assert zero compose delta in `visual_continuity_test`.

Product usage and keyboard controls: repository root `[README.md](../README.md)`.