#pragma once

#include "tools/mini_http_server.hpp"

namespace metaagent::cli {

class DroidHost;

// Builds the droidcli custom route table (connectors, tasks, status, config,
// ollama, process, command) as a tools::CustomRouteFn, tried after the
// built-in net::RouteTable (health/echo/notify/ai-chat) fails to match.
tools::CustomRouteFn make_droidcli_route_dispatch(DroidHost& host);

} // namespace metaagent::cli
