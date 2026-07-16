#include "reliability/path_guards.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::reliability;

	const std::string desktop = "C:\\Users\\luisarandas\\Desktop";

	// --- substitute_bare_desktop_token ---

	// Bare token preceded by nothing (Phase 7's original case).
	{
		const std::string result = substitute_bare_desktop_token("desktop/red.png", desktop);
		assert(result == desktop + "/red.png");
	}
	{
		const std::string result = substitute_bare_desktop_token("desktop\\red.png", desktop);
		assert(result == desktop + "\\red.png");
	}

	// Preceded by a space, inside a longer command line.
	{
		const std::string result = substitute_bare_desktop_token("-y -i in.mp4 desktop/out.mp4", desktop);
		assert(result == "-y -i in.mp4 " + desktop + "/out.mp4");
	}

	// Real transcript (Phase 18): a single leading "/" - write_file's path
	// argument used exactly this shape and previously landed in a bogus
	// "C:\Desktop\..." instead of the real Desktop.
	{
		const std::string result = substitute_bare_desktop_token("/Desktop/probe_test.png", desktop);
		assert(result == desktop + "/probe_test.png");
	}

	// Real transcript (Phase 23): a leading "./" - "desktop" starts at index
	// 2 there, one past the single-leading-separator case above.
	{
		const std::string result = substitute_bare_desktop_token("./desktop/new_folder", desktop);
		assert(result == desktop + "/new_folder");
	}

	// Deliberately NOT touched: an already-correct absolute path where
	// "Desktop" is preceded by more path, not a word boundary.
	{
		const std::string already_correct = desktop + "\\existing_file.txt";
		const std::string result = substitute_bare_desktop_token(already_correct, desktop);
		assert(result == already_correct);
	}

	// Empty desktop_path means resolution failed upstream - never touch the
	// input, never guess.
	{
		const std::string result = substitute_bare_desktop_token("desktop/red.png", "");
		assert(result == "desktop/red.png");
	}

	// --- default_bare_filename_to_desktop ---

	// Real transcript (Phase 20): a bare filename with no directory at all -
	// "output.png" - previously landed in droidcli's own working directory.
	{
		const std::string result = default_bare_filename_to_desktop("output.png", desktop);
		assert(result == desktop + "\\output.png");
	}

	// Anything with any directory information at all is left untouched, even
	// a relative one - the caller already specified where.
	{
		assert(default_bare_filename_to_desktop("subdir/output.png", desktop) == "subdir/output.png");
		assert(default_bare_filename_to_desktop("./output.png", desktop) == "./output.png");
	}

	// --- looks_like_placeholder_path ---

	// Real transcript (Phase 16): a documentation-convention template path.
	{
		assert(looks_like_placeholder_path("/path/to/Desktop/green_image.png"));
	}

	// Real transcript (Phase 23): "/Users/username/..." - the same
	// documentation-convention placeholder, a different specific word.
	{
		assert(looks_like_placeholder_path("/Users/username/Desktop/folder_name"));
	}

	// A real, legitimate resolved path must never be flagged.
	{
		assert(!looks_like_placeholder_path(desktop + "\\real_file.txt"));
	}

	// --- looks_like_invented_desktop_path ---

	// Real transcript (Phase 25): a literal root-level "C:\desktop\hello" -
	// looks absolute, but isn't the real per-user Desktop.
	{
		assert(looks_like_invented_desktop_path("C:\\desktop\\hello", desktop));
	}

	// The real, correctly-resolved Desktop path must never be flagged - this
	// is the exact false-positive risk the "bounded by separators, rooted at
	// the real prefix" logic exists to avoid.
	{
		assert(!looks_like_invented_desktop_path(desktop + "\\hello", desktop));
	}

	// A real folder that merely contains "desktop" as part of a longer name
	// (not a standalone path segment) must not false-positive.
	{
		assert(!looks_like_invented_desktop_path("C:\\Users\\luisarandas\\my_desktop_notes\\hello", desktop));
	}

	// Empty real_desktop_path means Desktop resolution itself failed - never
	// flag anything in that case, there's no known-good prefix to compare
	// against.
	{
		assert(!looks_like_invented_desktop_path("C:\\desktop\\hello", ""));
	}

	// --- looks_like_mismatched_binary_content ---

	// Real transcript (Phase 19): the literal ffmpeg command line written as
	// "content" for a .png path.
	{
		assert(looks_like_mismatched_binary_content(
			desktop + "\\output.png",
			"ffmpeg -y -f lavfi -i color=blue:s=512x512 -frames:v 1 -update 1 output.png"));
	}

	// Real transcript (Phase 19): the literal placeholder string used as
	// "content" for a .png path.
	{
		assert(looks_like_mismatched_binary_content(
			desktop + "\\blue.png",
			"<base64 encoded blue 512x512 pixel PNG image data>"));
	}

	// Real PNG magic bytes must be accepted, not rejected.
	{
		const std::string real_png_header = "\x89PNG\r\n\x1a\n" "restofdata";
		assert(!looks_like_mismatched_binary_content(desktop + "\\real.png", real_png_header));
	}

	// A path with no recognized binary/image extension is never checked -
	// this guard only catches the specific "claims to be a real media file,
	// isn't" shape.
	{
		assert(!looks_like_mismatched_binary_content(desktop + "\\notes.txt", "just plain text"));
	}

	std::cout << "path_guards_test passed" << std::endl;
	return 0;
}
