#include "intent/pending_command.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::intent;

	// The exact real transcript that motivated this: a fenced ffmpeg command
	// paired with "would you like me to execute this?".
	{
		const PendingCommand command = extract_proposed_command(
			"Here's the command for creating a 512x512 blue image:\n\n"
			"```ffmpeg -f lavfi -i color=blue:s=512x512 -frames:v 1 -update 1 Desktop/blue_image.png```\n\n"
			"Would you like me to execute this command as well?");
		assert(command.matched);
		assert(command.tool == "run_ffmpeg");
		assert(command.args == "-f lavfi -i color=blue:s=512x512 -frames:v 1 -update 1 Desktop/blue_image.png");
	}

	// A language-hint line on the fence's own line should be dropped, not
	// treated as part of the command.
	{
		const PendingCommand command = extract_proposed_command(
			"```ffmpeg\n-f lavfi -i color=red:s=256x256 out.png\n```\nShould I run this?");
		assert(command.matched);
		assert(command.tool == "run_ffmpeg");
		assert(command.args == "-f lavfi -i color=red:s=256x256 out.png");
	}

	// Bare (unfenced) ffmpeg line still recognized.
	{
		const PendingCommand command = extract_proposed_command(
			"You could run: ffmpeg -i in.mp4 -vf scale=640:-1 out.mp4\nWant me to execute this?");
		assert(command.matched);
		assert(command.tool == "run_ffmpeg");
		assert(command.args == "-i in.mp4 -vf scale=640:-1 out.mp4");
	}

	// A non-ffmpeg fenced command maps to run_command.
	{
		const PendingCommand command = extract_proposed_command(
			"```dir C:\\Users```\nWould you like me to run this?");
		assert(command.matched);
		assert(command.tool == "run_command");
		assert(command.args == "dir C:\\Users");
	}

	// A command shown as an example, with no offer to run it, must NOT match -
	// same false-positive caution as parse_open_intent.
	{
		const PendingCommand command = extract_proposed_command(
			"Here's how you'd do it yourself: ```ffmpeg -i in.mp4 out.mp4```");
		assert(!command.matched);
	}

	// Permission phrasing with no actual command anywhere must NOT match.
	{
		const PendingCommand command = extract_proposed_command(
			"Would you like me to execute this for you?");
		assert(!command.matched);
	}

	// Bare affirmatives.
	{
		assert(is_bare_affirmative("yes"));
		assert(is_bare_affirmative("Yes"));
		assert(is_bare_affirmative("y"));
		assert(is_bare_affirmative("  Yes!  "));
		assert(is_bare_affirmative("just do it"));
		assert(is_bare_affirmative("go ahead"));
		assert(is_bare_affirmative("sure."));
	}

	// Not a bare affirmative - a real follow-up request, not a confirmation.
	{
		assert(!is_bare_affirmative("Yes execute the command for the blue image and make it bigger"));
		assert(!is_bare_affirmative("no"));
		assert(!is_bare_affirmative("what does this command do"));
		assert(!is_bare_affirmative(""));
	}

	std::cout << "pending_command_test passed" << std::endl;
	return 0;
}
