#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::intent {

// Result of a deterministic, inference-free scan for "create a file called
// X" style phrasing.
struct CreateFileIntent {
	bool matched = false;
	// The extracted file name/path exactly as the user wrote it (e.g.
	// "notes.txt") - not yet resolved against the real Desktop. Same
	// division of labor as OpenIntent::app_name: recognition here, real-path
	// resolution left to write_file()'s own existing guards
	// (default_bare_filename_to_desktop, looks_like_placeholder_path, etc.).
	core::String file_name;
};

// Recognizes a narrow "create/make [a/a new] file called/named/titled X"
// shape, no LLM call - same discipline as parse_open_intent: false
// negatives (falling through to classify_via_llm) are always safe, so this
// stays deliberately narrow. Requires the whole word "file" (so "create a
// folder"/"create a task" never match) AND an explicit naming keyword
// ("called"/"named"/"titled") followed by real content (no naming keyword
// present means no guessing at a name - falls through instead). Maps to an
// empty-content write_file call; anything implying specific file content
// doesn't match this recognizer and reaches the full agent/LLM path.
DROIDCLI_API CreateFileIntent parse_create_file_intent(const core::String& message);

} // namespace droidcli::intent
