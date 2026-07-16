#include "reliability/claim_guards.hpp"

#include <cctype>

namespace droidcli::reliability {
namespace {

core::String claim_guards_to_lower_ascii(const core::String& value)
{
	core::String lowered;
	lowered.reserve(value.size());
	for (const unsigned char character : value)
	{
		lowered += static_cast<char>(std::tolower(character));
	}
	return lowered;
}

} // namespace

bool looks_like_unverified_action_claim(const core::String& text)
{
	const core::String lowered = claim_guards_to_lower_ascii(text);

	// Claims that work is continuing beyond this response - "I'm currently
	// running X", "the process is still ongoing", "give me a few more
	// seconds". These are unconditionally fabricated in this architecture,
	// not just probably: agent_turn is one bounded, synchronous request/
	// response - nothing droidcli does ever continues after this hop's HTTP
	// response is sent, there is no background job, no worker thread, no
	// "still working on it" state to report truthfully. Caught a real
	// specimen of this in practice: a model told the user "I am currently
	// using the 'list_open_windows' function" and, two turns later, "the
	// process is still ongoing... I will need a few more seconds" - zero
	// tool_calls were ever made for either claim. Unlike the claim+verb pair
	// below, a match here doesn't need a corroborating action verb - the
	// claim itself is the tell.
	static const char* const kOngoingProcessPhrases[] = {
		"is still ongoing", "still in progress", "in progress", "still ongoing",
		"still processing", "still working on", "currently using", "currently running",
		"i am currently", "i'm currently", "few more seconds", "a moment more",
		"not finished yet", "isn't finished yet", "give it a moment", "check back",
		"is it finished", "keep me posted"
	};
	for (const char* phrase : kOngoingProcessPhrases)
	{
		if (lowered.find(phrase) != core::String::npos)
		{
			return true;
		}
	}

	static const char* const kClaimPhrases[] = {
		// Future/in-progress.
		"i'll ", "i will ", "let me ", "hold on", "one moment",
		"give me a moment", "please wait", "processing this", "working on it",
		// Past-tense completion, unbacked by any tool call this turn.
		"i've ", "i have ", "has been ", "have been ", "successfully",
		"already ", "all done", "task is complete", "task complete"
	};
	bool has_claim = false;
	for (const char* phrase : kClaimPhrases)
	{
		if (lowered.find(phrase) != core::String::npos)
		{
			has_claim = true;
			break;
		}
	}
	if (!has_claim)
	{
		return false;
	}

	// Broader than just file/media verbs - covers the "I'll look that up"
	// class of claim too (search/list/find/check), which evaded this check
	// in the same real transcript that motivated kOngoingProcessPhrases
	// above ("I'll need to list all installed applications and then search
	// for those related to sound" had a claim phrase but no verb this list
	// used to recognize).
	static const char* const kActionVerbs[] = {
		"execute", "run ", "create", "created", "creating", "generate",
		"transcode", "convert", "save", "saved", "make", "made", "launch",
		"open", "write", "wrote", "delete", "deleted", "start", "move",
		"moved", "moving", "search", "searching", "list ", "listing",
		"find ", "finding", "check ", "checking", "look up", "looking up"
	};
	for (const char* verb : kActionVerbs)
	{
		if (lowered.find(verb) != core::String::npos)
		{
			return true;
		}
	}
	return false;
}

bool looks_like_degenerate_role_leak(const core::String& text)
{
	core::String trimmed = text;
	size_t start = 0;
	while (start < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[start])))
	{
		++start;
	}
	trimmed = trimmed.substr(start);

	static const char* const kRoleLabels[] = { "assistant", "system", "user" };
	for (const char* label : kRoleLabels)
	{
		const size_t label_length = std::char_traits<char>::length(label);
		if (trimmed.size() <= label_length)
		{
			continue;
		}
		bool matches_case_insensitive = true;
		for (size_t i = 0; i < label_length; ++i)
		{
			if (std::tolower(static_cast<unsigned char>(trimmed[i])) != label[i])
			{
				matches_case_insensitive = false;
				break;
			}
		}
		if (!matches_case_insensitive)
		{
			continue;
		}
		// The character right after the label must end the "word" (not part
		// of a real sentence like "Assistant managers..."), and what follows
		// must be whitespace/newline before any further content, not e.g.
		// "assistant: here's your answer" which is just an odd-but-real
		// prefix a model sometimes echoes - only a bare standalone label
		// followed by blank space counts as the leak this is meant to catch.
		const char next = trimmed[label_length];
		if (next == '\n' || next == '\r')
		{
			return true;
		}
	}
	return false;
}

bool looks_like_capability_denial(const core::String& text)
{
	const core::String lowered = claim_guards_to_lower_ascii(text);

	static const char* const kDenialPhrases[] = {
		"i can only assist", "beyond my capabilities", "beyond my capability",
		"i don't have the capability", "i do not have the capability",
		"i don't have the ability to execute", "i do not have the ability to execute",
		"i can't execute", "i cannot execute", "i can't run commands", "i cannot run commands",
		"i'm not able to run", "i am not able to run",
		"you'll need to run", "you will need to run", "run it yourself", "run this yourself",
		// A distinct fabricated-constraint variant caught in a real
		// transcript: not denying capability outright, but inventing a false
		// "only one at a time" limit as an excuse not to call the tool again
		// for a second, near-identical request (a second image, a retry).
		"only execute one command", "only run one command", "one command at a time",
		"execute one command at a time", "run one command at a time"
	};
	for (const char* phrase : kDenialPhrases)
	{
		if (lowered.find(phrase) != core::String::npos)
		{
			return true;
		}
	}
	return false;
}

} // namespace droidcli::reliability
