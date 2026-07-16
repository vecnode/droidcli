#include "reliability/command_guards.hpp"

#include <cctype>

namespace droidcli::reliability {
namespace {

core::String command_guards_to_lower_ascii(const core::String& value)
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

bool looks_like_destructive_command(const core::String& command_or_args)
{
	const core::String lowered = command_guards_to_lower_ascii(command_or_args);

	static const char* const kDestructivePatterns[] = {
		// Recursive/forced delete (Windows and POSIX shapes).
		"rm -rf", "rm -fr", "rm -r -f", "rm -f -r", "rd /s", "rmdir /s",
		"del /f /s", "del /s /f", "remove-item -recurse -force",
		// Disk/partition-level operations - irreversible by nature.
		"format ", "diskpart", "mkfs", "dd if=", "fdisk",
		// Broad forced process/service termination (not a single named
		// target - "taskkill /f /im *" or a bare "/f" with no /im at all
		// reads as "kill something forcibly, unclear what").
		"taskkill /f /im *", "shutdown /r", "shutdown /s", "shutdown -r", "shutdown -h",
		// A classic fork bomb.
		":(){ :|:& };:", ":(){:|:&};:",
	};
	for (const char* pattern : kDestructivePatterns)
	{
		if (lowered.find(pattern) != core::String::npos)
		{
			return true;
		}
	}
	return false;
}

} // namespace droidcli::reliability
