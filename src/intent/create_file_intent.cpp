#include "intent/create_file_intent.hpp"

#include "intent/phrase_strip.hpp"

namespace droidcli::intent {

CreateFileIntent parse_create_file_intent(const core::String& message)
{
	CreateFileIntent intent;

	const core::String courteous_stripped = trim_ascii(strip_leading_courtesy(message));
	const core::String lower_message = to_lower_ascii(courteous_stripped);

	static const char* kTriggerVerbs[] = {"create", "make"};
	size_t verb_length = 0;
	bool verb_found = false;
	for (const char* verb : kTriggerVerbs)
	{
		const core::String verb_str(verb);
		const bool is_prefix = lower_message.rfind(verb_str, 0) == 0;
		const bool boundary_ok = is_prefix
			&& (lower_message.size() == verb_str.size() || !is_word_char(lower_message[verb_str.size()]));
		if (is_prefix && boundary_ok)
		{
			verb_length = verb_str.size();
			verb_found = true;
			break;
		}
	}
	if (!verb_found)
	{
		return intent;
	}

	const core::String rest = trim_ascii(courteous_stripped.substr(verb_length));
	const core::String lower_rest = to_lower_ascii(rest);

	// Require "file" as a whole word - "create a folder called X"/"create a
	// task named X" correctly don't match, they name a different object.
	if (find_whole_word(lower_rest, "file") == core::String::npos)
	{
		return intent;
	}

	// Require an explicit naming keyword - no keyword means no name was
	// actually given, and this recognizer never guesses one.
	static const char* kNamingKeywords[] = {"called", "named", "titled"};
	size_t name_start = core::String::npos;
	for (const char* keyword : kNamingKeywords)
	{
		const core::String keyword_str(keyword);
		const size_t found = find_whole_word(lower_rest, keyword_str);
		if (found != core::String::npos)
		{
			const size_t candidate_start = found + keyword_str.size();
			if (name_start == core::String::npos || candidate_start < name_start)
			{
				name_start = candidate_start;
			}
		}
	}
	if (name_start == core::String::npos)
	{
		return intent;
	}

	core::String file_name = truncate_before_save_clause(rest.substr(name_start));
	file_name = strip_trailing_filler(file_name);
	file_name = strip_trailing_desktop_mention(file_name);
	file_name = strip_trailing_filler(file_name);

	if (file_name.empty())
	{
		return intent;
	}

	intent.matched = true;
	intent.file_name = file_name;
	return intent;
}

} // namespace droidcli::intent
