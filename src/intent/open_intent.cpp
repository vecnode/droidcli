#include "intent/open_intent.hpp"

#include <algorithm>
#include <cctype>

namespace droidcli::intent {
namespace {

core::String to_lower_ascii(const core::String& value)
{
	core::String result = value;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

core::String trim_ascii(const core::String& value)
{
	size_t start = 0;
	size_t end = value.size();
	while (start < end && std::isspace(static_cast<unsigned char>(value[start])))
	{
		++start;
	}
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
	{
		--end;
	}
	return value.substr(start, end - start);
}

bool is_word_char(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// Strips leading courtesy phrasing ("can you ", "please ", "hey ", ...) so
// the verb check below lines up on the actual request - "can you open
// notepad" and "open notepad" should be recognized identically.
core::String strip_leading_courtesy(core::String message)
{
	bool stripped = true;
	while (stripped)
	{
		stripped = false;
		message = trim_ascii(message);
		const core::String lower_message = to_lower_ascii(message);
		static const char* kCourtesyPrefixes[] = {
			"could you please ", "can you please ", "would you please ",
			"could you ", "can you ", "would you ", "please ", "hey "};
		for (const char* prefix : kCourtesyPrefixes)
		{
			const core::String prefix_str(prefix);
			if (lower_message.rfind(prefix_str, 0) == 0)
			{
				message = message.substr(prefix_str.size());
				stripped = true;
				break;
			}
		}
	}
	return message;
}

// Strips one leading filler word ("the", "a", "an", "up", "this", as in
// "open up notepad" or "open this thing"), repeated until none match. A
// filler word with nothing after it ("open the" with no content following
// "the") clears the target entirely rather than leaving the bare filler
// behind as a fake app name.
core::String strip_leading_filler(core::String target)
{
	bool stripped = true;
	while (stripped)
	{
		stripped = false;
		const core::String lower_target = to_lower_ascii(target);
		static const char* kLeadingFillerWords[] = {"up", "the", "a", "an", "this"};
		for (const char* filler_word : kLeadingFillerWords)
		{
			const core::String filler_str(filler_word);
			if (lower_target == filler_str)
			{
				return {};
			}
			const core::String filler_prefix = filler_str + " ";
			if (lower_target.rfind(filler_prefix, 0) == 0)
			{
				target = trim_ascii(target.substr(filler_prefix.size()));
				stripped = true;
				break;
			}
		}
	}
	return target;
}

// Strips trailing punctuation and trailing filler phrases ("please",
// "for me", "now", "right now"), repeated until none match.
core::String strip_trailing_filler(core::String target)
{
	bool stripped = true;
	while (stripped)
	{
		stripped = false;
		target = trim_ascii(target);
		while (!target.empty()
			&& (target.back() == '?' || target.back() == '!' || target.back() == '.' || target.back() == ','))
		{
			target.pop_back();
			stripped = true;
		}
		target = trim_ascii(target);

		const core::String lower_target = to_lower_ascii(target);
		static const char* kTrailingFillers[] = {"please", "for me", "right now", "now"};
		for (const char* filler : kTrailingFillers)
		{
			const core::String filler_str(filler);
			if (lower_target.size() < filler_str.size())
			{
				continue;
			}
			const size_t tail_start = lower_target.size() - filler_str.size();
			if (lower_target.compare(tail_start, filler_str.size(), filler_str) != 0)
			{
				continue;
			}
			const bool left_ok = tail_start == 0 || !is_word_char(lower_target[tail_start - 1]);
			if (left_ok)
			{
				target = trim_ascii(target.substr(0, tail_start));
				stripped = true;
				break;
			}
		}
	}
	return target;
}

} // namespace

OpenIntent parse_open_intent(const core::String& message)
{
	OpenIntent intent;

	const core::String courteous_stripped = trim_ascii(strip_leading_courtesy(message));
	const core::String lower_message = to_lower_ascii(courteous_stripped);

	// The trigger verb must be the very first word of the (courtesy-stripped)
	// message, not merely present anywhere in it - "open notepad" and "can
	// you open notepad" match, but "how do I open a file in Python" does
	// not, so ordinary questions still reach the full agent/LLM path instead
	// of being hijacked into an app-launch confirmation.
	static const char* kTriggerVerbs[] = {"open", "launch", "start"};
	size_t verb_length = 0;
	bool verb_found = false;
	for (const char* verb : kTriggerVerbs)
	{
		const core::String verb_str(verb);
		// is_prefix implies lower_message.size() >= verb_str.size(), so the
		// boundary_ok index below is only ever evaluated once that's true -
		// short-circuiting here matters, not just style: computing
		// lower_message[verb_str.size()] when the message is shorter than
		// the verb (e.g. an empty message) reads out of bounds.
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

	core::String target = trim_ascii(courteous_stripped.substr(verb_length));
	target = strip_leading_filler(target);
	target = strip_trailing_filler(target);

	if (target.empty())
	{
		return intent;
	}

	intent.matched = true;
	intent.app_name = target;
	return intent;
}

} // namespace droidcli::intent
