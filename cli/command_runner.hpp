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

// Runs `command` in `work_dir` (current directory if empty), waiting up to
// `timeout_ms` for it to finish. On timeout, the process is forcibly
// terminated and error_message is set, but any output captured before the
// timeout is still returned.
//
// `via_shell` (default true, matching the original behavior) routes through
// the platform shell (`cmd.exe /c` on Windows, `sh -c` on POSIX) - needed for
// shell features (pipes, redirects, env var expansion) but on Windows
// `cmd.exe`'s own command-line grammar re-tokenizes the whole string before
// the target program ever sees it, which can silently corrupt an argument
// that itself contains embedded double quotes (observed: an ffmpeg filter
// expression like `s="sin(2*PI*44)"` came out mangled, producing a bogus
// "filename ... syntax is incorrect" error with no ffmpeg output at all -
// cmd.exe never even got to launching ffmpeg correctly). Pass `via_shell =
// false` for a command that's just "<quoted program path> <args>" and needs
// no shell features - CreateProcess's own command-line parsing (the same
// convention every C program's argv uses) handles nested quotes correctly
// where cmd.exe's `/c` grammar does not. On POSIX this flag currently has no
// effect (both paths go through `sh -c`) - the corruption above is a
// cmd.exe-specific quirk, not a POSIX shell one.
CommandRunResult run_command_once(
	const core::String& command,
	const core::String& work_dir,
	int32_t timeout_ms = 30000,
	bool via_shell = true);

// The single, authoritative definition of "did this command actually
// succeed" - launched, exited zero, and no error_message (timeout/spawn
// failure both set one). Derived on demand from CommandRunResult's existing
// fields rather than stored as a field of its own, so there's no way for it
// to drift out of sync with them the way two independent inline
// `launched && exit_code == 0` checks (as DroidHost::install_ollama() and
// pull_ollama_model() used to each have their own copy of) could.
inline bool command_succeeded(const CommandRunResult& result)
{
	return result.launched && result.exit_code == 0 && result.error_message.empty();
}

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
