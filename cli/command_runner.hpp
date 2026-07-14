#pragma once

#include "droidcli.h"

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

} // namespace droidcli::cli
