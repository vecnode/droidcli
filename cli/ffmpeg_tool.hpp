#pragma once

#include "droidcli_core.h"
#include "command_runner.hpp"

namespace droidcli::cli {

// The ffmpeg actuator: lets the agent invoke the ffmpeg CLI (transcode,
// clip, extract audio, thumbnail, etc.) the same way run_command lets it run
// any shell command, but with the binary resolved for it instead of relying
// on the caller to know where ffmpeg lives. Distinct from src/media/ (which
// links libavcodec/libavformat directly for in-process decode/probe) - this
// shells out to the standalone ffmpeg.exe, useful for encode/filter graphs
// nothing in src/media/ exposes.

struct FfmpegResolveResult {
	bool ok = false;
	core::String resolved_path;
	core::String error_message;
};

// Resolves the ffmpeg executable: first via PATH (same lookup which_executable
// uses), then via the DROIDCLI_FFMPEG_ROOT env var's bin/ directory (the same
// variable cmake/FFmpeg.cmake reads to locate the vendored dev copy) so a
// contributor's build tree works out of the box without a system-wide
// install. Neither location is hardcoded into core - both are either PATH or
// an explicit env var, matching how the rest of the host resolves tools.
FfmpegResolveResult resolve_ffmpeg();

// Runs ffmpeg with `args` (a single raw argument string, e.g. "-y -i in.mp4
// -vf scale=1280:-1 out.mp4") via run_command_once, after resolving the
// binary. work_dir defaults to droidcli's cwd if empty; timeout_ms defaults
// to a longer window than run_command's (encodes can run well past 30s).
CommandRunResult run_ffmpeg(
	const core::String& args,
	const core::String& work_dir,
	int32_t timeout_ms = 120000);

} // namespace droidcli::cli
