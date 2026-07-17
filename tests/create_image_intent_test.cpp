#include "intent/create_image_intent.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::intent;

	// Direct phrasing, default file name.
	{
		const CreateImageIntent intent = parse_create_image_intent(
			"create an image 512x512 size and all red, save to Desktop");
		assert(intent.matched);
		assert(intent.width == 512);
		assert(intent.height == 512);
		assert(intent.color == "red");
		assert(intent.file_name == "image.png");
	}

	// "make", explicit name, courtesy prefix, and a "save to desktop" clause
	// after a comma - regression check: this used to extract "swatch.png,
	// save to desktop" as the file name in its entirety.
	{
		const CreateImageIntent intent = parse_create_image_intent(
			"can you make a 400x400 green image called swatch.png, save to desktop");
		assert(intent.matched);
		assert(intent.width == 400);
		assert(intent.height == 400);
		assert(intent.color == "green");
		assert(intent.file_name == "swatch.png");
	}

	// Same save-clause shape, no comma.
	{
		const CreateImageIntent intent = parse_create_image_intent(
			"create a 256x256 blue image named tile.png save to the desktop");
		assert(intent.matched);
		assert(intent.file_name == "tile.png");
	}

	// Missing dimensions - never guess a size, fall through to the LLM path.
	{
		const CreateImageIntent intent = parse_create_image_intent("create a red image");
		assert(!intent.matched);
	}

	// Missing color - never guess a color.
	{
		const CreateImageIntent intent = parse_create_image_intent("create a 512x512 image");
		assert(!intent.matched);
	}

	// Missing the word "image" entirely - names a different object.
	{
		const CreateImageIntent intent = parse_create_image_intent("create a 512x512 red file");
		assert(!intent.matched);
	}

	// A question mentioning "image" mid-sentence must not be hijacked.
	{
		const CreateImageIntent intent = parse_create_image_intent("how do I create a 512x512 red image in ffmpeg");
		assert(!intent.matched);
	}

	// Unrelated message.
	{
		const CreateImageIntent intent = parse_create_image_intent("what's the weather like today");
		assert(!intent.matched);
	}

	// Empty message.
	{
		const CreateImageIntent intent = parse_create_image_intent("");
		assert(!intent.matched);
	}

	std::cout << "create_image_intent_test passed" << std::endl;
	return 0;
}
