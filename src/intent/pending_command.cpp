#include "intent/pending_command.hpp"

#include <algorithm>
#include <cctype>

namespace droidcli::intent {
namespace {

core::String pending_command_to_lower_ascii(const core::String& value)
{
	core::String result = value;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

core::String pending_command_trim_ascii(const core::String& value)
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

bool contains_any(const core::String& lowered_haystack, const core::Array<const char*>& needles)
{
	for (const char* needle : needles)
	{
		if (lowered_haystack.find(needle) != core::String::npos)
		{
			return true;
		}
	}
	return false;
}

// Pulls the raw content of the first fenced code block ("```...```") out of
// `text`, untouched (no trimming, no language-hint handling - the caller
// does both, since how a leading "ffmpeg" line should be treated depends on
// whether it's a hint line by itself or the start of the actual command).
// Empty if there's no complete (opening + closing) fence pair.
core::String extract_first_fenced_block(const core::String& text)
{
	const size_t open_pos = text.find("```");
	if (open_pos == core::String::npos)
	{
		return {};
	}
	const size_t content_start = open_pos + 3;
	const size_t close_pos = text.find("```", content_start);
	if (close_pos == core::String::npos || close_pos <= content_start)
	{
		return {};
	}
	return text.substr(content_start, close_pos - content_start);
}

// Falls back to a bare line containing "ffmpeg " as a whole word - covers a
// model that wrote the command inline without fencing it.
core::String extract_bare_ffmpeg_line(const core::String& text)
{
	size_t line_start = 0;
	while (line_start <= text.size())
	{
		size_t line_end = text.find('\n', line_start);
		if (line_end == core::String::npos)
		{
			line_end = text.size();
		}
		const core::String line = pending_command_trim_ascii(text.substr(line_start, line_end - line_start));
		const core::String lowered_line = pending_command_to_lower_ascii(line);
		const size_t ffmpeg_pos = lowered_line.find("ffmpeg ");
		if (ffmpeg_pos != core::String::npos)
		{
			// From the "ffmpeg" token onward, not the whole line - a model
			// often prefixes the command with prose ("You could run: ffmpeg
			// ...") that isn't part of the args.
			return line.substr(ffmpeg_pos);
		}
		if (line_end >= text.size())
		{
			break;
		}
		line_start = line_end + 1;
	}
	return {};
}

} // namespace

PendingCommand extract_proposed_command(const core::String& assistant_text)
{
	PendingCommand result;

	const core::String lowered_text = pending_command_to_lower_ascii(assistant_text);
	static const core::Array<const char*> kPermissionPhrases = {
		"would you like me to execute", "would you like me to run",
		"want me to execute", "want me to run",
		"do you want me to execute", "do you want me to run",
		"should i execute", "should i run", "shall i execute", "shall i run",
		"like me to execute this", "like me to run this"};
	if (!contains_any(lowered_text, kPermissionPhrases))
	{
		return result;
	}

	const core::String fenced = extract_first_fenced_block(assistant_text);
	const bool from_fence = !fenced.empty();
	core::String content = from_fence ? fenced : extract_bare_ffmpeg_line(assistant_text);
	if (content.empty())
	{
		return result;
	}

	// A fence whose first line is a bare "ffmpeg" language hint (no spaces,
	// nothing else on that line) means the real command starts on the next
	// line - drop the hint line rather than treat it as part of the args.
	bool ffmpeg_hint_dropped = false;
	if (from_fence)
	{
		const size_t newline_pos = content.find('\n');
		if (newline_pos != core::String::npos)
		{
			const core::String first_line = pending_command_trim_ascii(content.substr(0, newline_pos));
			if (pending_command_to_lower_ascii(first_line) == "ffmpeg")
			{
				content = content.substr(newline_pos + 1);
				ffmpeg_hint_dropped = true;
			}
		}
	}
	content = pending_command_trim_ascii(content);
	if (content.empty())
	{
		return result;
	}

	const core::String lowered_content = pending_command_to_lower_ascii(content);
	if (ffmpeg_hint_dropped)
	{
		// The hint line already established this is ffmpeg - what remains
		// is the args themselves, no further "ffmpeg " prefix to strip.
		result.tool = "run_ffmpeg";
		result.args = content;
	}
	else if (lowered_content.rfind("ffmpeg ", 0) == 0)
	{
		result.tool = "run_ffmpeg";
		result.args = pending_command_trim_ascii(content.substr(7));
	}
	else
	{
		result.tool = "run_command";
		result.args = content;
	}

	if (result.args.empty())
	{
		return PendingCommand{};
	}

	result.matched = true;
	return result;
}

bool is_bare_affirmative(const core::String& message)
{
	core::String trimmed = pending_command_trim_ascii(message);
	while (!trimmed.empty()
		&& (trimmed.back() == '.' || trimmed.back() == '!' || trimmed.back() == '?'))
	{
		trimmed.pop_back();
	}
	trimmed = pending_command_trim_ascii(trimmed);
	const core::String lowered = pending_command_to_lower_ascii(trimmed);

	static const core::Array<const char*> kAffirmatives = {
		"yes", "y", "yeah", "yep", "yup", "sure", "sure thing", "ok", "okay",
		"do it", "go ahead", "please do", "just do it", "execute it", "run it",
		"go for it", "yes please", "yes, please"};
	for (const char* affirmative : kAffirmatives)
	{
		if (lowered == affirmative)
		{
			return true;
		}
	}
	return false;
}

} // namespace droidcli::intent
