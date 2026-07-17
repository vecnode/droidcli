#include "intent/create_image_intent.hpp"

#include "intent/phrase_strip.hpp"

#include <cctype>
#include <cstdlib>

namespace droidcli::intent {
namespace {

// One of ffmpeg's own recognized named colors (lavfi's color= filter) -
// never arbitrary text. Deliberately small: the common swatch colors, not
// ffmpeg's full ~140-name table, so a false negative here (an uncommon
// color name) just falls through to classify_via_llm rather than this
// recognizer guessing at something ffmpeg might not actually accept.
bool is_known_color_word(const core::String& lower_word, core::String& out_color)
{
	static const char* kKnownColors[] = {
		"red", "blue", "green", "yellow", "orange", "purple", "pink",
		"black", "white", "gray", "grey", "cyan", "magenta", "brown"};
	for (const char* color : kKnownColors)
	{
		if (lower_word == color)
		{
			out_color = color;
			return true;
		}
	}
	return false;
}

// Scans `lower_text` for a whole "word" token (letters only) and reports it
// via out_color if it's a known color name - used to find a color mentioned
// anywhere in the message, not just at a fixed position.
bool find_color_word(const core::String& lower_text, core::String& out_color)
{
	size_t index = 0;
	while (index < lower_text.size())
	{
		if (!std::isalpha(static_cast<unsigned char>(lower_text[index])))
		{
			++index;
			continue;
		}
		size_t end = index;
		while (end < lower_text.size() && std::isalpha(static_cast<unsigned char>(lower_text[end])))
		{
			++end;
		}
		if (is_known_color_word(lower_text.substr(index, end - index), out_color))
		{
			return true;
		}
		index = end;
	}
	return false;
}

// Scans for the first "<digits>x<digits>" pattern (e.g. "512x512") -
// ffmpeg's own -s WIDTHxHEIGHT syntax. Both values sanity-bounded so a
// stray, unrelated "1x1" or an absurd "999999x999999" doesn't match.
bool find_dimensions(const core::String& lower_text, int32_t& out_width, int32_t& out_height)
{
	for (size_t index = 0; index < lower_text.size(); ++index)
	{
		if (!std::isdigit(static_cast<unsigned char>(lower_text[index])))
		{
			continue;
		}
		size_t width_end = index;
		while (width_end < lower_text.size() && std::isdigit(static_cast<unsigned char>(lower_text[width_end])))
		{
			++width_end;
		}
		if (width_end >= lower_text.size() || lower_text[width_end] != 'x')
		{
			continue;
		}
		const size_t height_start = width_end + 1;
		size_t height_end = height_start;
		while (height_end < lower_text.size() && std::isdigit(static_cast<unsigned char>(lower_text[height_end])))
		{
			++height_end;
		}
		if (height_end == height_start)
		{
			continue;
		}

		const int32_t width = std::atoi(lower_text.substr(index, width_end - index).c_str());
		const int32_t height = std::atoi(lower_text.substr(height_start, height_end - height_start).c_str());
		if (width > 0 && width <= 8192 && height > 0 && height <= 8192)
		{
			out_width = width;
			out_height = height;
			return true;
		}
	}
	return false;
}

} // namespace

CreateImageIntent parse_create_image_intent(const core::String& message)
{
	CreateImageIntent intent;

	const core::String courteous_stripped = trim_ascii(strip_leading_courtesy(message));
	const core::String lower_message = to_lower_ascii(courteous_stripped);

	static const char* kTriggerVerbs[] = {"create", "make"};
	bool verb_found = false;
	for (const char* verb : kTriggerVerbs)
	{
		const core::String verb_str(verb);
		const bool is_prefix = lower_message.rfind(verb_str, 0) == 0;
		const bool boundary_ok = is_prefix
			&& (lower_message.size() == verb_str.size() || !is_word_char(lower_message[verb_str.size()]));
		if (is_prefix && boundary_ok)
		{
			verb_found = true;
			break;
		}
	}
	if (!verb_found)
	{
		return intent;
	}

	// All four of these must hold, no partial credit - this recognizer is
	// deliberately narrow (see the header comment).
	if (find_whole_word(lower_message, "image") == core::String::npos)
	{
		return intent;
	}
	int32_t width = 0;
	int32_t height = 0;
	if (!find_dimensions(lower_message, width, height))
	{
		return intent;
	}
	core::String color;
	if (!find_color_word(lower_message, color))
	{
		return intent;
	}

	core::String file_name = "image.png";
	static const char* kNamingKeywords[] = {"called", "named"};
	for (const char* keyword : kNamingKeywords)
	{
		const core::String keyword_str(keyword);
		const size_t found = find_whole_word(lower_message, keyword_str);
		if (found != core::String::npos)
		{
			core::String extracted = truncate_before_save_clause(courteous_stripped.substr(found + keyword_str.size()));
			extracted = strip_trailing_filler(extracted);
			extracted = strip_trailing_desktop_mention(extracted);
			extracted = strip_trailing_filler(extracted);
			if (!extracted.empty())
			{
				file_name = extracted;
			}
			break;
		}
	}

	intent.matched = true;
	intent.width = width;
	intent.height = height;
	intent.color = color;
	intent.file_name = file_name;
	return intent;
}

} // namespace droidcli::intent
