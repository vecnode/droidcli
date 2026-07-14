#pragma once

#include "tools/mini_http_server.hpp"

namespace droidcli::cli {

class DroidHost;

// Builds the droidcli custom route table (connectors, tasks, status, config,
// ollama, process, run, agent/turn) as a tools::CustomRouteFn, tried after the
// built-in net::RouteTable (health/echo/notify/ai-chat) fails to match. Every
// route here sits under /api/* and is gated by the bearer-token check in
// tools::MiniHttpServer::poll_once before this dispatcher is ever reached.
tools::CustomRouteFn make_droidcli_route_dispatch(DroidHost& host);

} // namespace droidcli::cli
