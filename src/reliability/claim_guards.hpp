#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::reliability {

// Heuristic: does `text` claim an action was performed, or promise one is
// about to be, without an accompanying tool call in this same response? Two
// related failure modes local tool-use models exhibit:
//  - future/in-progress: "I'll execute ffmpeg...", "hold on while I process
//    this" - narrated intent instead of a real tool_calls entry.
//  - past-tense fabrication: "The image has been successfully created",
//    "I've moved it to your Desktop" - the more dangerous of the two, since
//    it asserts something already happened when nothing did (seen in
//    practice: a model claimed a file move it never attempted, with zero
//    tool calls anywhere in that turn).
// Deliberately loose - a claim phrase plus an action verb, not real NLP -
// since run_agent_tool_loop (cli/host.cpp) only nudges instead of accepting
// this as final when no action this turn actually succeeded, so a false
// positive here costs one extra bounded model round-trip at worst, never a
// wrong final answer reaching the user.
DROIDCLI_API bool looks_like_unverified_action_claim(const core::String& text);

// Catches a distinct, previously-observed failure mode that
// looks_like_unverified_action_claim doesn't cover: a nudged small local
// model degrading into leaked role-token output instead of either calling
// the tool or giving a real answer - a real transcript showed a nudge after
// "open Blender" produce the literal reply "assistant\n\nYes, please." with
// no tool_calls at all. That text contains no claim phrase and no action
// verb (it isn't *claiming* anything, it's just broken), so it passed the
// existing heuristic and reached the user as if it were a real response to
// "can you open Blender." Detected structurally, not semantically: the reply
// starts with a bare chat-role label ("assistant"/"system"/"user") standing
// alone on its own line/segment - a real sentence never opens that way.
DROIDCLI_API bool looks_like_degenerate_role_leak(const core::String& text);

// A third, distinct lie: not falsely claiming an action happened, but
// falsely claiming the agent CAN'T act at all - directly contradicted by
// droidcli's whole design (see HostConfig::system_prompt, cli/host.hpp) and,
// in the real transcript that motivated this, by the same session having
// successfully run run_ffmpeg for an image minutes earlier. Observed in
// practice after a command-retry budget was exhausted: "I can only assist
// with tasks and provide information... execution of any commands or
// actions is beyond my capabilities. You'll need to run the command
// yourself." This is worse than a fabricated success - it actively
// discourages the user from asking again for something the agent can
// actually do.
DROIDCLI_API bool looks_like_capability_denial(const core::String& text);

} // namespace droidcli::reliability
