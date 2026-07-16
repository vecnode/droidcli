#include "reliability/command_guards.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::reliability;

	// Recursive/forced delete - POSIX and Windows shapes.
	{
		assert(looks_like_destructive_command("rm -rf /some/dir"));
		assert(looks_like_destructive_command("rm -fr /some/dir"));
		assert(looks_like_destructive_command("rd /s /q C:\\some\\dir"));
		assert(looks_like_destructive_command("del /f /s /q C:\\some\\dir"));
		assert(looks_like_destructive_command("Remove-Item -Recurse -Force C:\\some\\dir"));
	}

	// Disk/partition-level operations.
	{
		assert(looks_like_destructive_command("format D:"));
		assert(looks_like_destructive_command("diskpart"));
		assert(looks_like_destructive_command("dd if=/dev/zero of=/dev/sda"));
	}

	// Broad forced process kill / shutdown-reboot.
	{
		assert(looks_like_destructive_command("taskkill /f /im *"));
		assert(looks_like_destructive_command("shutdown /r /t 0"));
	}

	// A fork bomb.
	{
		assert(looks_like_destructive_command(":(){ :|:& };:"));
	}

	// Case-insensitive.
	{
		assert(looks_like_destructive_command("RM -RF /tmp/x"));
	}

	// Ordinary, non-destructive commands must never be flagged.
	{
		assert(!looks_like_destructive_command("dir C:\\Users\\luisarandas\\Desktop"));
		assert(!looks_like_destructive_command("echo hello"));
		assert(!looks_like_destructive_command("ffmpeg -y -i input.mp4 output.webm"));
		assert(!looks_like_destructive_command("git status"));
	}

	// A single, specific delete (not recursive, not broad) must not be
	// flagged - this guard is deliberately narrow to unambiguous shapes.
	{
		assert(!looks_like_destructive_command("del C:\\Users\\luisarandas\\Desktop\\one_file.txt"));
	}

	std::cout << "command_guards_test passed" << std::endl;
	return 0;
}
