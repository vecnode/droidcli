#include "intent/open_intent.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::intent;

	// Direct imperative phrasing.
	{
		const OpenIntent intent = parse_open_intent("Open Notepad");
		assert(intent.matched);
		assert(intent.app_name == "Notepad");
	}

	// Courtesy prefix + trailing filler + punctuation.
	{
		const OpenIntent intent = parse_open_intent("Can you open Blender for me?");
		assert(intent.matched);
		assert(intent.app_name == "Blender");
	}

	// "please" prefix, "launch" verb, leading article.
	{
		const OpenIntent intent = parse_open_intent("please launch the calculator");
		assert(intent.matched);
		assert(intent.app_name == "calculator");
	}

	// "start" verb, "up" filler, trailing "now".
	{
		const OpenIntent intent = parse_open_intent("start up chrome now");
		assert(intent.matched);
		assert(intent.app_name == "chrome");
	}

	// "this" filler.
	{
		const OpenIntent intent = parse_open_intent("open this notepad thing");
		assert(intent.matched);
		assert(intent.app_name == "notepad thing");
	}

	// Case-insensitive verb, mixed-case target preserved.
	{
		const OpenIntent intent = parse_open_intent("OPEN VsCode");
		assert(intent.matched);
		assert(intent.app_name == "VsCode");
	}

	// A genuine question containing "open" mid-sentence must NOT be hijacked
	// into an app-launch confirmation - it should fall through to the full
	// agent/LLM path instead.
	{
		const OpenIntent intent = parse_open_intent("how do I open a file in Python");
		assert(!intent.matched);
	}

	// "opening" is not the standalone word "open".
	{
		const OpenIntent intent = parse_open_intent("what are your opening hours");
		assert(!intent.matched);
	}

	// Verb with nothing left to open after filler-stripping.
	{
		const OpenIntent intent = parse_open_intent("open the");
		assert(!intent.matched);
	}

	// Unrelated message.
	{
		const OpenIntent intent = parse_open_intent("what's the weather like today");
		assert(!intent.matched);
	}

	// Empty message - regression check for an out-of-bounds operator[]
	// access when the message is shorter than the trigger verb being tested.
	{
		const OpenIntent intent = parse_open_intent("");
		assert(!intent.matched);
	}

	// Message shorter than every trigger verb, same regression class as above.
	{
		const OpenIntent intent = parse_open_intent("hi");
		assert(!intent.matched);
	}

	std::cout << "intent_test passed" << std::endl;
	return 0;
}
