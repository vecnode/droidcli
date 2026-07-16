#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::reliability {

// run_command/run_ffmpeg is droidcli's single largest blast-radius
// capability - arbitrary shell execution, gated by a human approval prompt
// that (before this) just showed the raw command string. That prompt's
// safety depends entirely on a human actually reading it every time, which
// gets harder the more routine approving becomes. This is not a safety
// mechanism on its own - it never blocks anything, tool_call_requires_approval
// (cli/host.cpp) is still the only real gate - it's a visibility aid: a
// narrow, evidence-shaped list of command patterns that are unambiguously
// destructive/irreversible (recursive delete, disk-formatting, partition
// tools, forced process/service kills targeting everything, shutdown/
// reboot), surfaced as an extra warning in the same approval prompt so a
// human skimming past a routine "yes" is more likely to actually stop and
// read this particular one. Deliberately conservative: a false negative
// (missing a destructive command) just means no extra warning, the normal
// approval flow is unaffected either way - so this stays narrow rather than
// trying to catch every possible destructive shape.
DROIDCLI_API bool looks_like_destructive_command(const core::String& command_or_args);

} // namespace droidcli::reliability
