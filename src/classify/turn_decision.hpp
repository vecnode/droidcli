#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::classify {

// What a chat turn should do, decided before any tool executes. Replaces the
// old design where the model both decided an action AND narrated the outcome
// across an open-ended multi-hop loop - see "Classify -> Execute -> Phrase"
// in ARCHITECTURE.md. A TurnDecision only ever describes at most one action;
// there is no concept of a chain of decisions within a single turn.
enum class TurnDecisionKind {
	// A deterministic recognizer (open_intent or pending_command) matched -
	// no LLM call was made at all.
	DeterministicTool,
	// No deterministic recognizer matched; the one LLM classification call
	// returned a tool call (its first, if it returned more than one - see
	// try_deterministic_classify's caller for why extras are discarded).
	LlmTool,
	// No deterministic recognizer matched; the one LLM classification call
	// returned no tool call, just conversational text.
	PlainReply,
	// Neither deterministic recognizer matched - caller must make the one
	// LLM classification call itself (needs ai::ModelProvider, host-side).
	NoDeterministicMatch,
	// The one LLM classification call itself failed at the transport/HTTP
	// level (not a narration failure - a real network/provider error).
	TransportFailed,
};

struct TurnDecision {
	TurnDecisionKind kind = TurnDecisionKind::NoDeterministicMatch;
	// Set iff kind is DeterministicTool or LlmTool - the exact tool_name/
	// arguments_json shape execute_agent_tool() already expects, so a
	// TurnDecision is a direct drop-in for the execution pipeline regardless
	// of which path produced it.
	core::String tool_name;
	core::String arguments_json;
	// True only for the pending_command match: the assistant's own previous
	// message already proposed this exact command and asked permission, and
	// the user's bare "yes" IS that permission - the caller must bypass
	// tool_call_requires_approval()'s normal gate for this one decision
	// rather than pausing to ask again for something already approved in
	// conversation. False for every other kind, including the open_intent
	// match ("open X" is a brand-new request with no prior approval, so an
	// open_application decision from it still goes through the normal gate
	// like any other).
	bool already_approved = false;
	// Set iff kind is PlainReply.
	core::String plain_reply_text;
	// Set iff kind is TransportFailed.
	core::String error_message;
};

// Tries the two existing deterministic recognizers, in order, and maps a
// match directly onto a resolved {tool_name, arguments_json} pair - the same
// shape execute_agent_tool() expects. Returns NoDeterministicMatch if neither
// fires; the caller (host-side, needs ai::ModelProvider) then makes the one
// LLM classification call itself. Pure string scanning + JSON-string
// building only, no I/O - open_intent.cpp/pending_command.cpp themselves are
// untouched, this is a thin composition layer over both, not a replacement.
//
// 1. intent::parse_open_intent(user_message) - "open X" style phrasing maps
//    to tool_name="open_application", arguments_json={"path_or_name":X}. The
//    raw, unresolved name is handed through as-is - resolving it to a real
//    path is execute_agent_tool()'s/the Windows execution ruleset's job
//    (open_application is already a gated tool going through
//    precheck_and_resolve_gated_call, unchanged by this), not this
//    function's.
// 2. intent::extract_proposed_command(previous_assistant_text) +
//    intent::is_bare_affirmative(user_message) - a bare "yes" confirming a
//    just-proposed command maps to tool_name="run_ffmpeg"/"run_command" with
//    the extracted command text as the "args"/"command" field - the exact
//    mapping agent_turn's old inline bypass block used to build by hand.
DROIDCLI_API TurnDecision try_deterministic_classify(
	const core::String& user_message,
	const core::String& previous_assistant_text);

} // namespace droidcli::classify
