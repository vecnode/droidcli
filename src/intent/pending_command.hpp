#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::intent {

// A command the assistant itself proposed in its own previous message and
// explicitly asked permission to run - extracted so a plain "yes" reply can
// be executed deterministically instead of asked back to the (unreliable)
// model to decide again from scratch. See "Phase 14" in ARCHITECTURE.md -
// motivated by a real transcript where the model proposed a valid ffmpeg
// command, asked "would you like me to execute this?", and then - after the
// user said "yes" - returned an empty response, then falsely claimed "I can
// only execute one command at a time" rather than ever calling the tool.
struct PendingCommand {
	bool matched = false;
	// "run_ffmpeg" or "run_command" - which agent tool this maps to.
	core::String tool;
	// The extracted command text: for run_ffmpeg, the ffmpeg argument string
	// with any leading "ffmpeg " stripped (matching what the run_ffmpeg tool's
	// "args" field expects); for run_command, the full shell command line.
	core::String args;
};

// Scans the assistant's own previous message for a proposed command paired
// with permission-asking phrasing ("would you like me to execute/run this?",
// "should I run this?", "want me to execute this?"). Requires BOTH a
// recognizable command (a fenced code block, or a bare line containing
// "ffmpeg ") AND the permission-asking phrase - a message that merely shows
// an example command (answering "how do I use ffmpeg") without asking to run
// it must not match, same false-positive caution as parse_open_intent.
DROIDCLI_API PendingCommand extract_proposed_command(const core::String& assistant_text);

// Deterministic, case-insensitive recognizer for a bare affirmative reply
// ("yes", "y", "yeah", "sure", "do it", "go ahead", "please do", "just do
// it", ...) - trimmed and punctuation-stripped. Pure string comparison
// against a small fixed list, not a heuristic - this is only ever checked
// after extract_proposed_command already matched, so the combined false-
// positive risk (both must be true) stays low without needing this half to
// be as permissive as parse_open_intent's courtesy-prefix stripping.
DROIDCLI_API bool is_bare_affirmative(const core::String& message);

} // namespace droidcli::intent
