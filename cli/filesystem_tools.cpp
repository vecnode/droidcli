#include "filesystem_tools.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace droidcli::cli {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

std::vector<core::String> split_path_env(const core::String& value)
{
	std::vector<core::String> parts;
	size_t start = 0;
	for (size_t index = 0; index <= value.size(); ++index)
	{
		if (index == value.size() || value[index] == kPathListSeparator)
		{
			if (index > start)
			{
				parts.push_back(value.substr(start, index - start));
			}
			start = index + 1;
		}
	}
	return parts;
}

} // namespace

FileReadResult read_file(const core::String& path, const int64_t max_bytes)
{
	FileReadResult result;
	if (path.empty())
	{
		result.error_message = "path is empty";
		return result;
	}

	std::error_code error;
	const fs::path target(path);
	if (!fs::exists(target, error) || error)
	{
		result.error_message = "path does not exist: " + path;
		return result;
	}
	if (fs::is_directory(target, error) || error)
	{
		result.error_message = "path is a directory, not a file: " + path;
		return result;
	}

	std::ifstream file(target, std::ios::binary);
	if (!file)
	{
		result.error_message = "could not open file: " + path;
		return result;
	}

	const int64_t file_size = static_cast<int64_t>(fs::file_size(target, error));
	if (error)
	{
		result.error_message = "could not determine file size: " + path;
		return result;
	}
	result.size_bytes = file_size;

	const int64_t read_limit = max_bytes > 0 ? max_bytes : file_size;
	const size_t to_read = static_cast<size_t>(file_size > read_limit ? read_limit : file_size);
	result.content.resize(to_read);
	if (to_read > 0)
	{
		file.read(&result.content[0], static_cast<std::streamsize>(to_read));
	}
	result.truncated = file_size > read_limit;
	result.ok = true;
	return result;
}

FileWriteResult write_file(const core::String& path, const core::String& content, const bool append_mode)
{
	FileWriteResult result;
	if (path.empty())
	{
		result.error_message = "path is empty";
		return result;
	}

	std::error_code error;
	const fs::path target(path);
	const fs::path parent = target.parent_path();
	if (!parent.empty() && !fs::exists(parent, error))
	{
		fs::create_directories(parent, error);
		if (error)
		{
			result.error_message = "could not create parent directories: " + error.message();
			return result;
		}
	}

	const std::ios::openmode mode = std::ios::binary | (append_mode ? std::ios::app : std::ios::trunc);
	std::ofstream file(target, mode);
	if (!file)
	{
		result.error_message = "could not open file for writing: " + path;
		return result;
	}

	file.write(content.data(), static_cast<std::streamsize>(content.size()));
	if (!file)
	{
		result.error_message = "write failed: " + path;
		return result;
	}

	result.ok = true;
	result.bytes_written = static_cast<int64_t>(content.size());
	return result;
}

ListDirResult list_dir(const core::String& path)
{
	ListDirResult result;
	const fs::path target = path.empty() ? fs::current_path() : fs::path(path);

	std::error_code error;
	if (!fs::exists(target, error) || error)
	{
		result.error_message = "path does not exist: " + path;
		return result;
	}
	if (!fs::is_directory(target, error) || error)
	{
		result.error_message = "path is not a directory: " + path;
		return result;
	}

	for (const fs::directory_entry& entry : fs::directory_iterator(target, error))
	{
		if (error)
		{
			break;
		}
		DirEntryInfo info;
		info.name = entry.path().filename().string();
		std::error_code entry_error;
		info.is_dir = entry.is_directory(entry_error);
		if (!info.is_dir)
		{
			info.size_bytes = static_cast<int64_t>(entry.file_size(entry_error));
		}
		result.entries.push_back(info);
	}
	if (error)
	{
		result.error_message = "error while listing directory: " + error.message();
		return result;
	}

	result.ok = true;
	return result;
}

StatResult stat_path(const core::String& path)
{
	StatResult result;
	if (path.empty())
	{
		result.error_message = "path is empty";
		return result;
	}

	std::error_code error;
	const fs::path target(path);
	result.exists = fs::exists(target, error);
	if (!result.exists)
	{
		result.ok = true;
		return result;
	}

	result.is_dir = fs::is_directory(target, error);
	result.is_file = fs::is_regular_file(target, error);
	if (result.is_file)
	{
		result.size_bytes = static_cast<int64_t>(fs::file_size(target, error));
	}
	result.ok = true;
	return result;
}

core::String get_current_working_directory()
{
	std::error_code error;
	const fs::path current = fs::current_path(error);
	if (error)
	{
		return {};
	}
	return current.string();
}

WhichResult which_executable(const core::String& name)
{
	WhichResult result;
	if (name.empty())
	{
		result.error_message = "name is empty";
		return result;
	}

	// A path separator in the name means the caller already gave a path -
	// just check it exists rather than searching PATH.
	const fs::path given(name);
	if (given.has_parent_path())
	{
		std::error_code error;
		if (fs::exists(given, error) && fs::is_regular_file(given, error))
		{
			result.ok = true;
			result.resolved_path = fs::absolute(given, error).string();
			return result;
		}
		result.error_message = "not found: " + name;
		return result;
	}

#ifdef _WIN32
	const std::vector<core::String> extensions = {"", ".exe", ".bat", ".cmd"};
#else
	const std::vector<core::String> extensions = {""};
#endif

	const char* path_env = std::getenv("PATH");
	const std::vector<core::String> directories = split_path_env(path_env != nullptr ? path_env : "");

	for (const core::String& directory : directories)
	{
		for (const core::String& extension : extensions)
		{
			const fs::path candidate = fs::path(directory) / (name + extension);
			std::error_code error;
			if (fs::exists(candidate, error) && fs::is_regular_file(candidate, error))
			{
				result.ok = true;
				result.resolved_path = candidate.string();
				return result;
			}
		}
	}

	result.error_message = "not found on PATH: " + name;
	return result;
}

FileOpResult copy_file(const core::String& source_path, const core::String& destination_path)
{
	FileOpResult result;
	if (source_path.empty() || destination_path.empty())
	{
		result.error_message = "source_path and destination_path are both required";
		return result;
	}

	std::error_code error;
	const fs::path source(source_path);
	if (fs::is_directory(source, error))
	{
		result.error_message = "source is a directory - copy_file only copies a single file";
		return result;
	}

	const fs::path destination(destination_path);
	const fs::path parent = destination.parent_path();
	if (!parent.empty() && !fs::exists(parent, error))
	{
		fs::create_directories(parent, error);
		if (error)
		{
			result.error_message = "could not create destination parent directories: " + error.message();
			return result;
		}
	}

	fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
	if (error)
	{
		result.error_message = "copy failed: " + error.message();
		return result;
	}
	result.ok = true;
	return result;
}

FileOpResult move_path(const core::String& source_path, const core::String& destination_path)
{
	FileOpResult result;
	if (source_path.empty() || destination_path.empty())
	{
		result.error_message = "source_path and destination_path are both required";
		return result;
	}

	std::error_code error;
	const fs::path source(source_path);
	if (!fs::exists(source, error))
	{
		result.error_message = "source does not exist: " + source_path;
		return result;
	}

	const fs::path destination(destination_path);
	const fs::path parent = destination.parent_path();
	if (!parent.empty() && !fs::exists(parent, error))
	{
		fs::create_directories(parent, error);
		if (error)
		{
			result.error_message = "could not create destination parent directories: " + error.message();
			return result;
		}
	}

	fs::rename(source, destination, error);
	if (error)
	{
		result.error_message = "move failed: " + error.message();
		return result;
	}
	result.ok = true;
	return result;
}

FileOpResult delete_file(const core::String& path)
{
	FileOpResult result;
	if (path.empty())
	{
		result.error_message = "path is empty";
		return result;
	}

	std::error_code error;
	const fs::path target(path);
	if (!fs::exists(target, error))
	{
		result.error_message = "path does not exist: " + path;
		return result;
	}
	if (fs::is_directory(target, error))
	{
		result.error_message = "path is a directory - delete_file only deletes a single file, not a directory tree";
		return result;
	}

	const bool removed = fs::remove(target, error);
	if (error || !removed)
	{
		result.error_message = "delete failed: " + (error ? error.message() : core::String("unknown error"));
		return result;
	}
	result.ok = true;
	return result;
}

FileOpResult create_directory(const core::String& path)
{
	FileOpResult result;
	if (path.empty())
	{
		result.error_message = "path is empty";
		return result;
	}

	std::error_code error;
	const fs::path target(path);
	if (fs::exists(target, error))
	{
		if (fs::is_directory(target, error))
		{
			// Idempotent - "create a folder" that's already there isn't a
			// failure, same as `mkdir -p`.
			result.ok = true;
			return result;
		}
		result.error_message = "path already exists as a file, not a directory: " + path;
		return result;
	}

	const bool created = fs::create_directories(target, error);
	if (error || !created)
	{
		result.error_message = "create_directory failed: " + (error ? error.message() : core::String("unknown error"));
		return result;
	}
	result.ok = true;
	return result;
}

} // namespace droidcli::cli
