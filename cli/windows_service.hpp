#pragma once

#include "core/types.hpp"

#include <functional>

// Part of droidcli-infra (ARCHITECTURE.md's Modules diagram) - the Service
// Control Manager is its own OS-specific surface, not droidcli-tools.
//
// Background service support: the highest-ranked "still open" item on droidcli's own
// hardening priorities list before this phase - `--daemon` was a documented
// no-op. `--headless` (skip the TUI, keep the HTTP loop) was already the
// correct foundation; this is one more host entry point around the same
// DroidHost, not a redesign.
namespace droidcli::cli {

constexpr const char* kWindowsServiceName = "droidcli";

// Blocks until the Windows Service Control Manager stops this service
// (SERVICE_CONTROL_STOP/SHUTDOWN), running `run_loop` on the calling thread
// in the meantime - `run_loop` should poll/tick until the bool reference it
// receives goes false, exactly like droidcli's existing --headless loop
// already does with its own g_running flag (cli/droidcli.cpp). `on_stop`
// runs once, after run_loop returns, before reporting SERVICE_STOPPED - the
// same place save_state()/server.stop() already run for a normal foreground
// exit.
//
// Returns false immediately (without blocking) if this process was not
// actually launched by the SCM - e.g. run directly from a console for
// testing - so the caller can report a clear error instead of silently
// hanging forever waiting for a control-manager connection that will never
// come. Always returns false on a non-Windows build.
bool run_as_windows_service(
	const std::function<void(volatile bool&)>& run_loop,
	const std::function<void()>& on_stop);

// Registers/unregisters droidcli as a Windows Service via the Service
// Control Manager (CreateService/DeleteService) - requires an elevated
// (Administrator) process; both report why to stderr on failure rather than
// just returning false silently. `command_line` should include the full
// invocation the SCM should launch, e.g. "\"C:\\...\\droidcli.exe\" --service
// --headless --settings db/droidcli_settings.json" - deliberately no
// --token/--anthropic-api-key on it: those come from the settings file (see
// settings_store.hpp), never from a command line any process on the machine
// can read (Task Manager, `wmic process`, the Event Log). No-ops (return
// false) on a non-Windows build.
bool install_windows_service(const core::String& command_line);
bool uninstall_windows_service();

} // namespace droidcli::cli
