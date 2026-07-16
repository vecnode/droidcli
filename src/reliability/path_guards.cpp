#include "reliability/path_guards.hpp"

#include <cctype>

namespace droidcli::reliability {
namespace {

core::String path_guards_to_lower_ascii(const core::String& value)
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

core::String substitute_bare_desktop_token(const core::String& args, const core::String& desktop_path)
{
	if (desktop_path.empty())
	{
		return args;
	}

	const core::String lowered = path_guards_to_lower_ascii(args);

	core::String result;
	result.reserve(args.size() + desktop_path.size());
	size_t cursor = 0;
	while (cursor < args.size())
	{
		const bool leading_separator = cursor == 1 && (args[0] == '/' || args[0] == '\\');
		const bool leading_dot_separator = cursor == 2 && args[0] == '.' && (args[1] == '/' || args[1] == '\\');
		const bool at_word_boundary = cursor == 0
			|| args[cursor - 1] == ' ' || args[cursor - 1] == '"' || args[cursor - 1] == '\''
			|| leading_separator || leading_dot_separator;
		const bool matches_slash = lowered.compare(cursor, 8, "desktop/") == 0;
		const bool matches_backslash = lowered.compare(cursor, 8, "desktop\\") == 0;

		if (at_word_boundary && (matches_slash || matches_backslash))
		{
			// The leading_separator/leading_dot_separator cases above already
			// appended those 1-2 literal prefix characters to result during
			// the earlier cursor==0(/1) iterations - drop them here so the
			// substitution doesn't leave a stray "/" or "./" in front of
			// desktop_path's own drive letter (e.g. "/C:\Users\..." or
			// "./C:\Users\...").
			if ((leading_separator || leading_dot_separator) && result.size() >= cursor)
			{
				result.resize(result.size() - cursor);
			}
			result += desktop_path;
			result += matches_slash ? '/' : '\\';
			cursor += 8;
			continue;
		}

		result += args[cursor];
		++cursor;
	}
	return result;
}

core::String default_bare_filename_to_desktop(const core::String& path, const core::String& desktop_path)
{
	if (path.empty() || desktop_path.empty())
	{
		return path;
	}
	if (path.find('/') != core::String::npos || path.find('\\') != core::String::npos)
	{
		return path;
	}
	const char last_char = desktop_path.back();
	const bool needs_separator = last_char != '/' && last_char != '\\';
	return desktop_path + (needs_separator ? "\\" : "") + path;
}

bool looks_like_placeholder_path(const core::String& path)
{
	const core::String lowered = path_guards_to_lower_ascii(path);
	return lowered.find("path/to/") != core::String::npos
		|| lowered.find("path\\to\\") != core::String::npos
		|| lowered.find("<") != core::String::npos
		|| lowered.find("your_file") != core::String::npos
		|| lowered.find("yourfile") != core::String::npos
		|| lowered.find("example.txt") != core::String::npos
		|| lowered.find("filename.ext") != core::String::npos
		// Substring match also catches "yourusername".
		|| lowered.find("username") != core::String::npos;
}

bool looks_like_invented_desktop_path(const core::String& resolved_path, const core::String& real_desktop_path)
{
	if (real_desktop_path.empty())
	{
		return false;
	}

	const core::String lowered_path = path_guards_to_lower_ascii(resolved_path);
	const core::String lowered_desktop = path_guards_to_lower_ascii(real_desktop_path);

	if (lowered_path.rfind(lowered_desktop, 0) == 0)
	{
		// Already correctly rooted at the real Desktop.
		return false;
	}

	size_t search_position = 0;
	while (true)
	{
		const size_t found = lowered_path.find("desktop", search_position);
		if (found == core::String::npos)
		{
			return false;
		}
		const bool left_boundary = found == 0 || lowered_path[found - 1] == '/' || lowered_path[found - 1] == '\\';
		const size_t after = found + 7;
		const bool right_boundary = after == lowered_path.size() || lowered_path[after] == '/' || lowered_path[after] == '\\';
		if (left_boundary && right_boundary)
		{
			return true;
		}
		search_position = after;
	}
}

bool looks_like_mismatched_binary_content(const core::String& path, const core::String& content)
{
	const core::String lowered_path = path_guards_to_lower_ascii(path);

	struct BinarySignature
	{
		const char* extension;
		const char* magic_bytes;
		size_t magic_length;
	};
	static const BinarySignature kSignatures[] = {
		{".png", "\x89PNG\r\n\x1a\n", 8},
		{".jpg", "\xFF\xD8\xFF", 3},
		{".jpeg", "\xFF\xD8\xFF", 3},
		{".gif", "GIF8", 4},
		{".bmp", "BM", 2},
	};

	for (const BinarySignature& signature : kSignatures)
	{
		const core::String extension(signature.extension);
		if (lowered_path.size() < extension.size()
			|| lowered_path.compare(lowered_path.size() - extension.size(), extension.size(), extension) != 0)
		{
			continue;
		}
		const core::String magic_bytes(signature.magic_bytes, signature.magic_length);
		return content.size() < magic_bytes.size() || content.compare(0, magic_bytes.size(), magic_bytes) != 0;
	}
	return false;
}

} // namespace droidcli::reliability
