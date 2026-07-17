#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::classify {

// Deterministic, zero-LLM-call phrasing for the common tool/outcome shapes -
// the "Phrase" step of Classify -> Execute -> Phrase (see ARCHITECTURE.md).
// Reads the same "ok"/error/resolved-path fields the model would otherwise
// be trusted to narrate itself, and only ever describes what's actually in
// result_json - never invents anything beyond it. Pure JSON-field
// extraction + string building, no I/O. Returns an empty string if no
// template matches this (tool_name, result_json) shape; the caller falls
// back to a narrow, grounded LLM phrasing call instead
// (DroidHost::phrase_via_llm, cli/host.cpp).
//
// The tool names handled here are a subset of agent_tool_definitions()'s -
// this list must never grow into a second, independent mapping of tool
// behavior; each branch only ever reads result_json's fields, never
// re-implements what the tool itself does.
DROIDCLI_API core::String try_template_reply(
	const core::String& tool_name,
	const core::String& arguments_json,
	const core::String& result_json);

} // namespace droidcli::classify
