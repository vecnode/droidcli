#pragma once

#include "host.hpp"

namespace metaagent::cli {

// Runs the default interactive FTXUI terminal dashboard for droidcli. Blocks
// the calling thread until the user quits (q / Ctrl+C), at which point it
// sets running_flag = false and returns. The HTTP API server must already be
// running on another thread - this function only drives the terminal UI, it
// never touches sockets directly (all state comes from DroidHost's already
// thread-safe, JSON-returning accessor methods).
//
// http_port is only used for display (e.g. a status line showing where the
// HTTP API is reachable while the TUI is up).
int run_tui(DroidHost& host, int http_port, volatile bool& running_flag);

} // namespace metaagent::cli
