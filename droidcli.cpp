/**
 * Droidcli amalgamated implementation.
 * All library .cpp units live under src/ and are compiled here.
 */

#include "core/types.cpp"
#include "core/math.cpp"
#include "core/log_sink.cpp"

#include "media/decode.cpp"
#include "media/probe.cpp"
#include "media/store.cpp"
#include "media/corpus.cpp"

#include "app/commands.cpp"
#include "app/runtime_catalog.cpp"
#include "app/tasks.cpp"
#include "runtime/host_interfaces.cpp"
#include "ai/types.cpp"
#include "ai/ollama_client.cpp"
#include "ai/language_runtime.cpp"
#include "net/json.cpp"
#include "net/handlers.cpp"
#include "net/router.cpp"
#include "net/signal_types.cpp"
#include "net/signal_router.cpp"
#include "net/connector.cpp"
#include "notify/parse.cpp"
#include "session/status.cpp"

#include "initialize.cpp"
