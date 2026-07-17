#pragma once

#include "core/types.hpp"

namespace droidcli::classify {

// What a chat turn should do, decided before any tool executes. Replaces the
// old design where the model both decided an action AND narrated the outcome
// across an open-ended multi-hop loop - see "Classify -> Execute -> Phrase"
// in ARCHITECTURE.md. A TurnDecision only ever describes at most one action;
// there is no concept of a chain of decisions within a single turn. The one
// LLM classification call (classify_via_llm, cli/host.cpp) is the sole
// source of a TurnDecision - there is no deterministic pre-classification
// step anymore.
enum class TurnDecisionKind {
	// The one LLM classification call returned a tool call (its first, if it
	// returned more than one - see classify_via_llm's caller for why extras
	// are discarded).
	LlmTool,
	// The one LLM classification call returned no tool call, just
	// conversational text.
	PlainReply,
	// The one LLM classification call itself failed at the transport/HTTP
	// level (not a narration failure - a real network/provider error).
	TransportFailed,
};

struct TurnDecision {
	TurnDecisionKind kind = TurnDecisionKind::PlainReply;
	// Set iff kind is LlmTool - the exact tool_name/arguments_json shape
	// execute_agent_tool() already expects.
	core::String tool_name;
	core::String arguments_json;
	// Set iff kind is PlainReply.
	core::String plain_reply_text;
	// Set iff kind is TransportFailed.
	core::String error_message;
};

} // namespace droidcli::classify
