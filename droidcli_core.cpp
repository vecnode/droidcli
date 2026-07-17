/**
 * Droidcli amalgamated implementation.
 * All library .cpp units live under src/ and are compiled here.
 */

#include "core/types.cpp"
#include "core/math.cpp"
#include "core/log_sink.cpp"
#include "core/spawn.cpp"

#include "media/decode.cpp"
#include "media/probe.cpp"
#include "media/store.cpp"

#include "app/tasks.cpp"
#include "ai/types.cpp"
#include "ai/ollama_client.cpp"
#include "ai/anthropic_client.cpp"
#include "ai/model_provider.cpp"
#include "ai/language_runtime.cpp"
#include "net/json.cpp"
#include "net/handlers.cpp"
#include "net/router.cpp"
#include "net/connector.cpp"
#include "notify/parse.cpp"
#include "session/status.cpp"
#include "reliability/command_guards.cpp"
#include "reliability/path_guards.cpp"
#include "classify/response_templates.cpp"

#include "initialize.cpp"
