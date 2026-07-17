#include "intent/phrase_strip.hpp"

#include <algorithm>
#include <cctype>

namespace droidcli::intent {

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

bool is_word_char(const char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

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
			"could you ", "can you ", "would you ", "please ", "hey ",
			// Real-world lead-ins observed in practice - "Ok I want you to
			// open Blender" should recognize the same intent as "open
			// Blender" once these are peeled off one at a time (the outer
			// while loop above re-runs this whole list after each strip, so
			// "ok " then "i want you to " both fire in turn).
			"i want you to ", "i would like you to ", "i'd like you to ",
			"i want to ", "i would like to ", "i'd like to ",
			// "Ok I basically want you to open X" fell through to the LLM
			// path the same way the "great "/"now " gap above once did - "i "
			// alone isn't a prefix (see the multi-word entries above), so an
			// adverb wedged between "i" and "want" broke the match entirely
			// rather than just needing one more strip-pass.
			"i basically want you to ", "i basically want to ",
			"i really want you to ", "i really want to ",
			"i just want you to ", "i just want to ",
			"so, ", "so ", "okay, ", "okay ", "ok, ", "ok ", "well, ", "well ",
			// Acknowledgement/filler words observed sitting between a courtesy
			// prefix and the verb in a real transcript - "Ok great can you now
			// open Blender?" fell through to the (unreliable) LLM path because
			// neither "great " nor "now " was stripped, leaving "great can you
			// now open blender?" with no verb at position 0 after the loop gave
			// up. The outer while loop already re-runs this whole list after
			// each strip, so "ok " -> "great " -> "can you " -> "now " all peel
			// off in turn the same way "ok "/"i want you to " already did.
			"great ", "cool ", "nice ", "awesome ", "sure ", "alright ",
			"yes ", "yeah ", "now ", "just ", "quickly ", "really "};
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

core::String strip_trailing_desktop_mention(core::String target)
{
	static const char* kTrailingDesktopPhrases[] = {
		"on the desktop", "on my desktop", "to the desktop", "to my desktop", "on desktop"};
	bool stripped = true;
	while (stripped)
	{
		stripped = false;
		target = trim_ascii(target);
		const core::String lower_target = to_lower_ascii(target);
		for (const char* phrase : kTrailingDesktopPhrases)
		{
			const core::String phrase_str(phrase);
			if (lower_target.size() < phrase_str.size())
			{
				continue;
			}
			const size_t tail_start = lower_target.size() - phrase_str.size();
			if (lower_target.compare(tail_start, phrase_str.size(), phrase_str) != 0)
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

core::String truncate_before_save_clause(core::String text)
{
	const size_t comma = text.find(',');
	if (comma != core::String::npos)
	{
		text = text.substr(0, comma);
	}

	const core::String lower_text = to_lower_ascii(text);
	static const char* kSaveClauseWords[] = {"save", "saved", "stored"};
	for (const char* word : kSaveClauseWords)
	{
		const size_t found = find_whole_word(lower_text, word);
		if (found != core::String::npos && found > 0)
		{
			text = text.substr(0, found);
			break;
		}
	}
	return trim_ascii(text);
}

size_t find_whole_word(const core::String& lower_haystack, const core::String& lower_word)
{
	size_t search_from = 0;
	while (true)
	{
		const size_t found = lower_haystack.find(lower_word, search_from);
		if (found == core::String::npos)
		{
			return core::String::npos;
		}
		const bool left_ok = found == 0 || !is_word_char(lower_haystack[found - 1]);
		const size_t after = found + lower_word.size();
		const bool right_ok = after == lower_haystack.size() || !is_word_char(lower_haystack[after]);
		if (left_ok && right_ok)
		{
			return found;
		}
		search_from = found + 1;
	}
}

} // namespace droidcli::intent
