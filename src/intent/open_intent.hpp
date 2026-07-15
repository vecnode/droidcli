#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::intent {

// Result of a deterministic, inference-free scan for "open X" style phrasing.
struct OpenIntent {
	bool matched = false;
	// Trimmed target text as the user wrote it, e.g. "Notepad" or "the
	// calculator" - not yet resolved against any app index.
	core::String app_name;
};

// Recognizes a narrow set of app-launch phrasings ("open X", "launch X",
// "start X", optionally prefixed with "please"/"can you"/"could you" and
// suffixed with "please"/"for me"/trailing punctuation) without any LLM
// call - pure string scanning so a host can react to "open notepad"
// instantly and deterministically instead of waiting on Ollama to decide
// whether to call a tool. Deliberately narrow: anything that doesn't match
// this shape falls through to the full agent tool-calling loop, which can
// still handle richer phrasing via the LLM.
DROIDCLI_API OpenIntent parse_open_intent(const core::String& message);

} // namespace droidcli::intent
