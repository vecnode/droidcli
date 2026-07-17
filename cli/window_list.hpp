#pragma once

#include "droidcli_core.h"

#include <cstdint>

namespace droidcli::cli {

// Part of droidcli-infra (ARCHITECTURE.md's Modules diagram) - executes an
// OS-specific enumeration (EnumWindows) directly, not droidcli-tools.
//
// One currently-open, visible top-level window - "what's open right now",
// distinct from app_index's "what's installed" (may not be running) and
// ProcessManager's "what droidcli itself launched" (only covers
// launched_process connectors, not every running app).
struct OpenWindowInfo {
	core::String title;
	core::String process_name;
	int64_t pid = 0;
};

// Enumerates currently visible top-level windows (the same set Alt+Tab
// shows: has a title, is actually visible), each with its owning process
// name and PID. Windows-only; returns an empty array on other platforms.
// Called fresh on every request - not cached, since "open right now" is
// inherently a live snapshot.
core::Array<OpenWindowInfo> list_open_windows();

} // namespace droidcli::cli
