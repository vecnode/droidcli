#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <cstdint>

namespace droidcli::intent {

// Result of a deterministic, inference-free scan for "create an NxN
// solid-color image" style phrasing.
struct CreateImageIntent {
	bool matched = false;
	int32_t width = 0;
	int32_t height = 0;
	// One of a small fixed whitelist of ffmpeg-recognized color names (see
	// create_image_intent.cpp) - never arbitrary text.
	core::String color;
	// Defaults to "image.png" if the message gives no explicit name.
	core::String file_name;
};

// Recognizes a narrow "create/make a WxH <color> image [called X]" shape,
// no LLM call. Deliberately narrow - all of these must hold, no partial
// credit: the trigger verb at the start of the (courtesy-stripped) message,
// the whole word "image" present, a WxH digit pattern present, and a color
// word from the fixed whitelist present. A false negative (falling through
// to classify_via_llm) is always safe; this exists so the common "solid
// color swatch" case never depends on the model hand-authoring an ffmpeg
// command - see try_deterministic_classify (classify/turn_decision.cpp),
// which turns a match into a fully code-constructed run_ffmpeg call.
DROIDCLI_API CreateImageIntent parse_create_image_intent(const core::String& message);

} // namespace droidcli::intent
