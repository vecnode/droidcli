#pragma once

/**
 * MetaAgent portable library — public entry point.
 *
 * Implementation is compiled from a single translation unit:
 *   metaagent/metaagent.cpp
 *
 * Embed in other engines by adding `metaagent/src` to your include path and
 * compiling `metaagent.cpp` once (see MetaAgentCoreAggregate.cpp in the UE plugin).
 */

#include "export.hpp"
#include "version.hpp"
#include "initialize.hpp"

#include "core/log_sink.hpp"
#include "core/math.hpp"
#include "core/types.hpp"

#include "media/decode.hpp"
#include "media/image.hpp"
#include "media/mask_cache.hpp"
#include "media/pipeline.hpp"
#include "media/probe.hpp"
#include "media/store.hpp"
#include "media/corpus.hpp"

#include "camera/controller.hpp"
#include "camera/rig.hpp"
#include "camera/types.hpp"

#include "particle/actuation_math.hpp"
#include "particle/forming_solver.hpp"
#include "particle/forming_types.hpp"
#include "particle/image_mask_processor.hpp"
#include "particle/pattern_types.hpp"
#include "particle/representation_actuation.hpp"
#include "particle/representation_types.hpp"
#include "particle/return_types.hpp"
#include "particle/scheduler.hpp"
#include "particle/shape_builder.hpp"
#include "particle/shape_types.hpp"
#include "particle/transition_graph.hpp"
#include "particle/visual_continuity.hpp"
#include "particle/effect_catalog.hpp"
#include "particle/state_effects.hpp"

#include "runtime/host_interfaces.hpp"
#include "ai/language_runtime.hpp"
#include "ai/ollama_client.hpp"
#include "ai/types.hpp"
#include "app/gui_actions.hpp"
#include "app/gui_catalog.hpp"
#include "app/runtime_catalog.hpp"
#include "input/policy.hpp"
#include "net/handlers.hpp"
#include "net/json.hpp"
#include "net/router.hpp"
#include "net/platform_client.hpp"
#include "net/signal_router.hpp"
#include "net/signal_types.hpp"
#include "net/types.hpp"
#include "notify/parse.hpp"
#include "session/status.hpp"
#include "session/types.hpp"
