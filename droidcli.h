#pragma once

/**
 * Droidcli portable library — public entry point.
 *
 * Implementation is compiled from a single translation unit:
 *   droidcli/droidcli.cpp
 *
 * Embed in other applications by adding `droidcli/src` to your include path
 * and compiling `droidcli.cpp` once.
 */

#include "export.hpp"
#include "version.hpp"
#include "initialize.hpp"

#include "core/log_sink.hpp"
#include "core/math.hpp"
#include "core/types.hpp"

#include "media/decode.hpp"
#include "media/image.hpp"
#include "media/probe.hpp"
#include "media/store.hpp"

#include "runtime/host_interfaces.hpp"
#include "ai/language_runtime.hpp"
#include "ai/ollama_client.hpp"
#include "ai/types.hpp"
#include "app/commands.hpp"
#include "app/runtime_catalog.hpp"
#include "net/handlers.hpp"
#include "net/json.hpp"
#include "net/router.hpp"
#include "net/types.hpp"
#include "notify/parse.hpp"
#include "session/status.hpp"
#include "session/types.hpp"
