#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::intent {

DROIDCLI_API core::String to_lower_ascii(const core::String& value);

DROIDCLI_API core::String trim_ascii(const core::String& value);

DROIDCLI_API bool is_word_char(char c);

// Strips leading courtesy phrasing ("can you ", "please ", "hey ", ...) so a
// verb check downstream lines up on the actual request - "can you open
// notepad" and "open notepad" should be recognized identically. Shared by
// every deterministic intent recognizer (open_intent, create_file_intent,
// create_image_intent) so the ~30-entry courtesy-prefix list lives in one
// place, not copied per recognizer.
DROIDCLI_API core::String strip_leading_courtesy(core::String message);

// Strips trailing punctuation and trailing filler phrases ("please",
// "for me", "now", "right now"), repeated until none match.
DROIDCLI_API core::String strip_trailing_filler(core::String target);

// Finds `lower_word` in `lower_haystack` as a whole word (not a substring of
// a larger word) - e.g. "file" matches "a file called" but not "profiler"
// or "filename". Both arguments must already be lowercased by the caller
// (this does no case conversion of its own). Returns core::String::npos if
// not found.
DROIDCLI_API size_t find_whole_word(const core::String& lower_haystack, const core::String& lower_word);

// Strips a trailing "on the desktop"/"on my desktop"/"to the desktop"/"on
// desktop" phrase - Desktop is already the default target
// (default_bare_filename_to_desktop, cli/host.cpp), so this is redundant
// information to drop from an extracted name, not a location override to
// honor. Shared by create_file_intent and create_image_intent.
DROIDCLI_API core::String strip_trailing_desktop_mention(core::String target);

// Truncates `text` at the first comma, or the first whole-word "save"/
// "saved"/"stored" if that comes first - both are strong, general signals
// that whatever follows is a separate "where to put it" clause, not part of
// an extracted name. A real transcript showed "create ... called
// swatch.png, save to desktop" extract "swatch.png, save to desktop" as the
// file name before this existed - enumerating every possible save-location
// phrasing doesn't scale, this structural cut does. Call before
// strip_trailing_filler/strip_trailing_desktop_mention, not after.
DROIDCLI_API core::String truncate_before_save_clause(core::String text);

} // namespace droidcli::intent
