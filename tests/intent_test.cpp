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

	// Real-world phrasing observed in production: "Ok I want you to open
	// Blender" was NOT recognized before the courtesy-prefix list grew to
	// cover "ok "/"i want you to " - it fell through to Ollama, which
	// refused ("I don't have the capability to directly open applications")
	// even though open_application existed and worked fine moments later
	// for a differently-phrased request.
	{
		const OpenIntent intent = parse_open_intent("Ok I want you to open Blender");
		assert(intent.matched);
		assert(intent.app_name == "Blender");
	}

	// Real-world phrasing observed in a second production incident: "Ok great
	// can you now open Blender?" fell through to the LLM path because neither
	// "great " nor "now " (sitting between the courtesy prefix and the verb)
	// was stripped - the model then took two hops (one fabrication nudge) and
	// still never called open_application, replying with garbled leaked-role
	// text instead ("assistant\n\nYes, please.") before eventually launching
	// Blender on a later, differently-phrased retry the user had to make by hand.
	{
		const OpenIntent intent = parse_open_intent("Ok great can you now open Blender?");
		assert(intent.matched);
		assert(intent.app_name == "Blender");
	}

	// "so, open X" and "I'd like to open X" variants.
	{
		const OpenIntent intent = parse_open_intent("So, open Chrome");
		assert(intent.matched);
		assert(intent.app_name == "Chrome");
	}
	{
		const OpenIntent intent = parse_open_intent("I'd like to open Discord");
		assert(intent.matched);
		assert(intent.app_name == "Discord");
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
