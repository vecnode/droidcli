# metaagent — Architecture

Portable C++17 library for MetaAgent **domain logic**: particle pattern mechanics, camera rig math, media/mask pipeline, HTTP route handlers, session snapshots, command validation, and input policy. Unreal (or another host) supplies I/O, rendering, and engine APIs through thin bridges.

---

## Design goals

| Goal | How |
|------|-----|
| Portability | C++17, `metaagent::core::*` value types (no `FVector` / `FString`) |
| Single source of truth | FSM, solvers, phase curves, actuation compose, scheduler, shape/mask algorithms, camera pose math, HTTP handler bodies |
| Testability | CMake + unit tests without the editor |
| Engine bridge | Host converts types and supplies I/O callbacks (Niagara, PNG, world queries, HTTPServer bind, view-target blend) |

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

| Seam | Status |
|------|--------|
| **Visual pose** | **Done** — `DisplayedPose`, `freeze_displayed_pose()`, `apply_visual_continuity_for_transition()` |
| **Particle I/O** | **Done** — `ParticleHostCallbacks` on `SchedulerCallbacks::particle_host` |
| **Shape / mask** | Core algorithms + UE async cache; host supplies PNG load |
| **Representation delivery** | Core policy + UE driver registry |
| **Commands / GUI** | **Done** — catalog + validation; AI + Recording panel rows |
| **Session snapshot** | Planned — extend `/health` with particle FSM summary |

---

## Repository layout

```
metaagent/
├── metaagent.h                    Umbrella public API
├── metaagent.cpp                  Single TU — #includes all module .cpp files
├── src/
│   ├── initialize.hpp             initialize_defaults()
│   ├── core/                      Vec3, math, log_sink
│   ├── media/                     PNG/JPEG decode, MediaStore, mask pipeline
│   ├── camera/                    Zoom + cinematic rig/controller
│   ├── particle/                  Pattern domain (FSM, scheduler, actuation, shapes, visual_continuity)
│   ├── net/                       Route table, handlers, platform_client
│   ├── notify/                    Notify body parsing
│   ├── session/                   RuntimeSession + status strings
│   ├── app/                       Command registry, GUI catalog, action validation
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

| Area | In `metaagent` (portable) | Stays in UE plugin (host) |
|------|---------------------------|---------------------------|
| **Particles** | FSM, scheduler, actuation, solvers, shape/mask math, state effects | Niagara buffers, direct capture, orchestrator UX, UObject runtime |
| **Camera** | Orbit pose, sway, zoom, `CameraController` | `SetViewTargetWithBlend`, focus queries, observation lock |
| **HTTP inbound** | `/health`, `/echo`, `/notify` handlers + router | Epic `HTTPServer` bind/listen |
| **HTTP outbound** | URL/body build + response parse | Async POST transport (`FHttpModule`) |
| **Session / commands** | `RuntimeSession`, `validate_command`, `validate_gui_action` | Key binds, HUD draw, dispatch |
| **Input policy** | `policy_for_runtime()` | Enhanced Input, mouse hit-test |
| **GUI panel** | `gui_catalog`, action validation | Canvas draw, click regions |

---

## Particle domain (`metaagent/particle/`)

| Module | Key API | Role |
|--------|---------|------|
| `pattern_types` | `PatternConfig`, `PatternRuntime` | FSM state, buffers, curve samples |
| `transition_graph` | `TransitionGraph` | Declarative FSM table |
| `scheduler` | `ParticleScheduler`, `SchedulerCallbacks` | Tick, transitions, representation frame, **visual continuity via `particle_host`** |
| `visual_continuity` | `DisplayedPose`, `freeze_displayed_pose()` | Per-edge pose freeze using displayed (on-screen) positions |
| `forming_solver` | `FormingSolverRegistry` | Per-particle forming / return motion |
| `actuation_math` | `ActuationMath` | Phase evaluation, position composition |
| `representation_actuation` | `RepresentationActuationPolicy` | Direct / Parameters / Hybrid delivery |
| `representation_types` | `RepresentationMapping` | Macro phase from pattern state |
| `shape_builder` | `ShapeBuilder` | Targets, frames, silhouette assignment |
| `image_mask_processor` | `image_mask::build_mask_from_rgba` | Mask + stratified scatter |
| `state_effects` | `StateEffectStack` | Ambient breathing + optional cohesion/turbulence |
| `effect_catalog` | GUI particle action specs | Load preview vs trigger effect ID |

### Pattern FSM

States: `Idle`, `Preparing`, `Anticipating`, `Forming`, `Holding`, `Returning`, `Dissipating`, `Releasing`.

The scheduler advances via `TransitionGraph::evaluate_transition()`. The UE plugin calls through `MetaAgentParticleCoreBridge` — **no parallel FSM table in the plugin**.

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

| Module | Role |
|--------|------|
| `types` | `ZoomSettings`, `CinematicSettings`, `CinematicRuntimeState`, `FocusTarget`, `CinematicStyle` |
| `rig` | `compute_cinematic_pose`, zoom, focus-from-bounds |
| `controller` | Per-session `CameraController`: enable/disable cinematic, tick pose, zoom |

Styles: `OscillatingHold` (sway + hold), `SlowOrbit` (continuous yaw). Cycle via **V** or GUI.

Focus resolution (particle bounds, locked observation target) remains host-side in `FMetaAgentCameraRuntime::ResolveFocusTarget`.

---

## App / session / net / input

| Module | Role |
|--------|------|
| `session/types` | `RuntimeSession`, `FeatureFlags` |
| `app/commands` | `CommandId`, parse + validate |
| `app/gui_catalog` | Panel sections, rows, action IDs |
| `app/gui_actions` | GUI action string IDs → commands |
| `input/policy` | Block move/look in observation mode; allow wheel zoom when GUI closed |
| `net/router` + `handlers` | `/health`, `/echo`, `/notify` |
| `net/platform_client` | Outbound platform POST build/parse |
| `runtime/host_interfaces` | Recording + AI snapshots/toggles; **ParticleHostCallbacks** |

---

## UE plugin split

| Plugin path | Role |
|-------------|------|
| `MetaAgentCoreAggregate.cpp` | Embeds `metaagent/metaagent.cpp` |
| `MetaAgentTypeBridge.*` | UE ↔ core conversion, scheduler bridge, camera sync |
| `MetaAgentParticleRuntime.*` | UObject instance, Niagara tick glue, **ReadDisplayedPose / ApplyHostWorldPositions** |
| `MetaAgentParticleControl.*` | Orchestrator, drivers, Niagara profiles |
| `MetaAgentParticleShapes.*` | PNG load, mask cache, shape providers |
| `MetaAgentPlayerController.*` | Input, camera host, GUI dispatch, **host service snapshots** |
| `Host/MetaAgentHttpBridge.*` | Inbound HTTP |
| `Host/MetaAgentPlatformBridge.*` | Outbound HTTP |
| `Host/MetaAgentHostSession.*` | Session snapshot |
| `Host/MetaAgentInputBridge.*` | Command / GUI validation |
| `Host/MetaAgentHostServicesBridge.*` | `HostServiceCallbacks` → recording + AI |

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

## Roadmap

| Phase | Goal | Status |
|-------|------|--------|
| A | Particle FSM + actuation in core | Done |
| B | Inbound HTTP + session/commands in core | Done |
| C | Host bridges (Http, HostSession, Input) | Done |
| D | Outbound platform client + PlatformBridge | Done |
| E | GUI panel catalog + dispatch validation | Done |
| F | Camera style registry (`SlowOrbit`) | Done |
| G | Particle effect catalog in core | Done |
| H | Standalone `metaagent_server` CLI | Done |
| I | Recording / AI host_interfaces | Done |
| J | Manual FSM profile (Preparing, no anticipating on `.`) | Done |
| K | Host-side displayed-pose hold + authoritative count | Done (superseded by L) |
| **L** | **Core `DisplayedPose` + `freeze_displayed_pose` + continuity tests** | **Done** |
| M | `ParticleHostCallbacks` on scheduler | Done |
| N | Wire recording/AI `HostServiceCallbacks` in UE + GUI rows | Done |
| O | Authoritative count in core `PatternRuntime` | Planned |
| P | Headless particle simulator (mock host callbacks) for CI | Future |

---

## Build

### Standalone

```powershell
cd metaagent
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests: `transition_graph_test`, `forming_types_test`, `shape_builder_polyline_test`, `actuation_composer_test`, `media_decode_test`, `camera_rig_test`, `net_handler_test`, `app_command_test`, `gui_actions_test`, `platform_client_test`, `gui_catalog_test`, `effect_catalog_test`, `host_interfaces_test`, `state_effects_test`, **`visual_continuity_test`**.

### Unreal

`MetaAgentCoreAggregate.cpp` includes portable sources; `MetaAgentPlugin.Build.cs` adds `metaagent/src`.

---

## Extension points

1. **New forming mode** — solver in `forming_solver.cpp`, register in `initialize_defaults()`, mirror enum in TypeBridge.
2. **New shape source** — UE provider in shape registry; portable assignment in `shape_builder.cpp`.
3. **New camera style** — `CinematicStyle` + `compute_cinematic_pose` + UE enum/sync.
4. **New HTTP route** — handler in `net/handlers.cpp`, register in router.
5. **New validated command** — `CommandId` + `validate_command` + host handler + optional GUI action ID.
6. **New FSM transition** — row in `transition_graph.cpp` + case in `apply_visual_continuity_for_transition()` + test in `transition_graph_test.cpp` / `visual_continuity_test.cpp`.
7. **Visual continuity on new edge** — add branch in `apply_visual_continuity_for_transition()` + assert zero compose delta in `visual_continuity_test`.

Product usage and keyboard controls: repository root [`README.md`](../README.md).
