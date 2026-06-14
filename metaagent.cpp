/**
 * MetaAgent amalgamated implementation.
 * All library .cpp units live under src/ and are compiled here.
 */

#include "core/types.cpp"
#include "core/math.cpp"
#include "core/log_sink.cpp"

#include "media/decode.cpp"
#include "media/store.cpp"
#include "media/pipeline.cpp"
#include "media/mask_cache.cpp"

#include "camera/types.cpp"
#include "camera/rig.cpp"
#include "camera/controller.cpp"

#include "particle/forming_types.cpp"
#include "particle/return_types.cpp"
#include "particle/pattern_types.cpp"
#include "particle/representation_types.cpp"
#include "particle/transition_graph.cpp"
#include "particle/forming_solver.cpp"
#include "particle/actuation_math.cpp"
#include "particle/representation_actuation.cpp"
#include "particle/shape_types.cpp"
#include "particle/shape_builder.cpp"
#include "particle/image_mask_processor.cpp"
#include "particle/scheduler.cpp"
#include "particle/visual_continuity.cpp"

#include "app/commands.cpp"
#include "app/gui_actions.cpp"
#include "app/gui_catalog.cpp"
#include "input/policy.cpp"
#include "particle/effect_catalog.cpp"
#include "particle/state_effects.cpp"
#include "runtime/host_interfaces.cpp"
#include "ai/types.cpp"
#include "ai/ollama_client.cpp"
#include "ai/language_runtime.cpp"
#include "net/json.cpp"
#include "net/handlers.cpp"
#include "net/router.cpp"
#include "net/platform_client.cpp"
#include "notify/parse.cpp"
#include "session/status.cpp"

#include "initialize.cpp"
