#include "intent/create_file_intent.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::intent;

	// Direct imperative phrasing.
	{
		const CreateFileIntent intent = parse_create_file_intent("create a file called notes.txt");
		assert(intent.matched);
		assert(intent.file_name == "notes.txt");
	}

	// "make", "named", courtesy prefix, and a redundant "on the desktop" tail.
	{
		const CreateFileIntent intent = parse_create_file_intent(
			"can you make me a new file named todo.md on the desktop?");
		assert(intent.matched);
		assert(intent.file_name == "todo.md");
	}

	// "titled", trailing "please".
	{
		const CreateFileIntent intent = parse_create_file_intent("create a text file titled plan.txt please");
		assert(intent.matched);
		assert(intent.file_name == "plan.txt");
	}

	// A "save to desktop" clause after a comma - regression check for the
	// same bug found in create_image_intent (see its test for detail).
	{
		const CreateFileIntent intent = parse_create_file_intent(
			"create a file called report.txt, save to desktop");
		assert(intent.matched);
		assert(intent.file_name == "report.txt");
	}

	// No naming keyword - never guess a name, fall through to the LLM path.
	{
		const CreateFileIntent intent = parse_create_file_intent("create a file for my notes");
		assert(!intent.matched);
	}

	// Names a different object entirely - "folder", not "file".
	{
		const CreateFileIntent intent = parse_create_file_intent("create a folder called Projects");
		assert(!intent.matched);
	}

	// A question mentioning "file" mid-sentence must not be hijacked.
	{
		const CreateFileIntent intent = parse_create_file_intent("how do I create a file in Python");
		assert(!intent.matched);
	}

	// Unrelated message.
	{
		const CreateFileIntent intent = parse_create_file_intent("what's the weather like today");
		assert(!intent.matched);
	}

	// Empty message - regression check matching parse_open_intent's own.
	{
		const CreateFileIntent intent = parse_create_file_intent("");
		assert(!intent.matched);
	}

	std::cout << "create_file_intent_test passed" << std::endl;
	return 0;
}
