#pragma once

#include "droidcli_core.h"

#include <cstdint>

namespace droidcli::cli {

// One-shot, synchronous, blocking command execution with captured
// stdout/stderr - distinct from ProcessManager, which PID-tracks long-running
// launched_process connectors. Used by DroidHost::run_command() (POST
// /api/run) and by the "run" task-queue command.
struct CommandRunResult {
	bool launched = false;
	int32_t exit_code = 0;
	core::String stdout_text;
	core::String stderr_text;
	core::String error_message;
};

// Runs `command` (via the platform shell) in `work_dir` (current directory if
// empty), waiting up to `timeout_ms` for it to finish. On timeout, the
// process is forcibly terminated and error_message is set, but any output
// captured before the timeout is still returned.
CommandRunResult run_command_once(
	const core::String& command,
	const core::String& work_dir,
	int32_t timeout_ms = 30000);

struct LaunchAppResult {
	bool launched = false;
	int64_t pid = 0;
	core::String error_message;
};

// Starts `path_or_name` (resolved against PATH if it's a bare name, same as
// which_executable) with optional `args`, detached and fire-and-forget - no
// waiting for exit, no stdout/stderr capture. Distinct from run_command_once,
// which blocks until the command finishes: GUI applications don't exit on
// their own, so waiting for them would hang. Not PID-tracked by
// ProcessManager - this is a one-off "open this app" launch, not a
// registered launched_process connector.
LaunchAppResult launch_application(
	const core::String& path_or_name,
	const core::String& args,
	const core::String& work_dir);

} // namespace droidcli::cli
