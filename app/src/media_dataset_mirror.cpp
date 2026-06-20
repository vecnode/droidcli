#include "media_dataset_mirror.hpp"

#include "media/corpus.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>

namespace metaagent::app_host {
namespace {

namespace fs = std::filesystem;

bool has_media_extension(const fs::path& path)
{
	static const std::array<const char*, 13> k_extensions = {
		".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".tif", ".tiff",
		".mp4", ".mov", ".avi", ".mkv", ".webm"};

	core::String ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	for (const char* candidate : k_extensions)
	{
		if (ext == candidate)
		{
			return true;
		}
	}
	return false;
}

core::String media_type_for_path(const fs::path& path)
{
	core::String ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv" || ext == ".webm")
	{
		return "video";
	}
	return "image";
}

bool looks_like_json_array(const core::String& json)
{
	for (char c : json)
	{
		if (!std::isspace(static_cast<unsigned char>(c)))
		{
			return c == '[';
		}
	}
	return false;
}

core::String canonical_key(const fs::path& input)
{
	std::error_code ec;
	fs::path canon = fs::weakly_canonical(input, ec);
	if (ec)
	{
		canon = fs::absolute(input, ec);
		if (ec)
		{
			canon = input;
		}
	}

	core::String key = canon.make_preferred().string();
	std::transform(key.begin(), key.end(), key.begin(), [](const unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return key;
}

void add_candidate(
	core::Array<fs::path>& out,
	std::set<core::String>& seen,
	const fs::path& candidate)
{
	if (candidate.empty())
	{
		return;
	}

	const core::String key = canonical_key(candidate);
	if (seen.insert(key).second)
	{
		out.push_back(candidate);
	}
}

core::Array<fs::path> build_data_dir_candidates(const core::String& preferred)
{
	core::Array<fs::path> candidates;
	std::set<core::String> seen;

	if (!preferred.empty())
	{
		add_candidate(candidates, seen, fs::path(preferred));
	}

	std::error_code ec;
	fs::path cwd = fs::current_path(ec);
	if (ec)
	{
		cwd = fs::path{"."};
	}

	fs::path cursor = cwd;
	for (int depth = 0; depth < 8; ++depth)
	{
		add_candidate(candidates, seen, cursor / "data");
		add_candidate(candidates, seen, cursor / "bin" / "data");
		add_candidate(candidates, seen, cursor / "media-player-cpp" / "bin" / "data");
		add_candidate(candidates, seen, cursor / "apps" / "myApps" / "media-player-cpp" / "bin" / "data");
		add_candidate(candidates, seen,
			cursor / "of_v0.12.1_msys2_mingw64_release" / "apps" / "myApps" / "media-player-cpp" / "bin" / "data");

		if (!cursor.has_parent_path())
		{
			break;
		}
		cursor = cursor.parent_path();
	}

	return candidates;
}

bool directory_has_corpus_markdown(const fs::path& dir)
{
	std::error_code ec;
	return fs::exists(dir / "PDF_TEXT.md", ec) || fs::exists(dir / "OBJS_TEXT.md", ec);
}

core::String extract_file_key_from_heading(const core::String& line)
{
	if (line.rfind("## ", 0) != 0)
	{
		return {};
	}

	const std::size_t first_tick = line.find('`');
	if (first_tick == core::String::npos)
	{
		return {};
	}

	const std::size_t second_tick = line.find('`', first_tick + 1);
	if (second_tick == core::String::npos || second_tick <= first_tick + 1)
	{
		return {};
	}

	const core::String raw = line.substr(first_tick + 1, second_tick - first_tick - 1);
	return media::corpus_file_key_from_path(raw);
}

void append_markdown_file_keys(const fs::path& markdown_path, core::Array<core::String>& out_keys)
{
	std::ifstream stream(markdown_path.string());
	if (!stream.is_open())
	{
		return;
	}

	core::String line;
	while (std::getline(stream, line))
	{
		const core::String key = extract_file_key_from_heading(line);
		if (!key.empty())
		{
			out_keys.push_back(key);
		}
	}
}

} // namespace

void MediaDatasetMirror::set_preferred_data_directory(const core::String& data_directory)
{
	preferred_data_directory_ = data_directory;
	resolved_data_directory_.clear();
	local_clip_paths_.clear();
	local_index_attempted_ = false;
}

void MediaDatasetMirror::remember_remote_clips_json(const core::String& json)
{
	if (looks_like_json_array(json))
	{
		cached_remote_clips_json_ = json;
	}
}

void MediaDatasetMirror::ensure_local_index()
{
	if (local_index_attempted_)
	{
		return;
	}
	local_index_attempted_ = true;
	local_clip_paths_.clear();

	const core::Array<fs::path> candidates = build_data_dir_candidates(preferred_data_directory_);
	fs::path selected;
	std::error_code ec;

	for (const fs::path& candidate : candidates)
	{
		ec.clear();
		if (!fs::exists(candidate, ec) || !fs::is_directory(candidate, ec))
		{
			continue;
		}

		if (directory_has_corpus_markdown(candidate))
		{
			selected = candidate;
			break;
		}

		for (const auto& entry : fs::recursive_directory_iterator(candidate, ec))
		{
			if (ec)
			{
				break;
			}
			if (!entry.is_regular_file(ec))
			{
				continue;
			}
			if (has_media_extension(entry.path()))
			{
				selected = candidate;
				break;
			}
		}

		if (!selected.empty())
		{
			break;
		}
	}

	if (selected.empty())
	{
		return;
	}

	resolved_data_directory_ = selected.make_preferred().string();
	const fs::path data_dir = selected;
	core::Array<core::String> markdown_keys;
	append_markdown_file_keys(data_dir / "PDF_TEXT.md", markdown_keys);
	append_markdown_file_keys(data_dir / "PDF_TEXT2.md", markdown_keys);
	append_markdown_file_keys(data_dir / "OBJS_TEXT.md", markdown_keys);

	std::map<core::String, core::String> path_by_key;
	for (const auto& entry : fs::recursive_directory_iterator(selected, ec))
	{
		if (ec)
		{
			break;
		}
		if (!entry.is_regular_file(ec) || !has_media_extension(entry.path()))
		{
			continue;
		}

		fs::path preferred = entry.path();
		preferred.make_preferred();
		const core::String full_path = preferred.string();
		const core::String key = media::corpus_file_key_from_path(full_path);
		if (!key.empty() && path_by_key.find(key) == path_by_key.end())
		{
			path_by_key.emplace(key, full_path);
		}
	}

	std::set<core::String> seen_keys;
	for (const core::String& key : markdown_keys)
	{
		if (!seen_keys.insert(key).second)
		{
			continue;
		}

		auto it = path_by_key.find(key);
		if (it != path_by_key.end())
		{
			local_clip_paths_.push_back(it->second);
		}
	}

	if (local_clip_paths_.empty())
	{
		for (const auto& pair : path_by_key)
		{
			local_clip_paths_.push_back(pair.second);
		}
	}
}

core::String MediaDatasetMirror::build_clips_json_fallback()
{
	ensure_local_index();
	if (local_clip_paths_.empty())
	{
		return cached_remote_clips_json_.empty() ? "[]" : cached_remote_clips_json_;
	}

	core::String json = "[";
	for (std::size_t i = 0; i < local_clip_paths_.size(); ++i)
	{
		if (i > 0)
		{
			json += ',';
		}

		const fs::path clip_path(local_clip_paths_[i]);
		const core::String name = clip_path.filename().string();
		const core::String media_type = media_type_for_path(clip_path);

		json += "{";
		json += "\"index\":" + std::to_string(i);
		json += "," + net::json_string_field("name", name);
		json += "," + net::json_string_field("path", clip_path.string());
		json += "," + net::json_string_field("mediaType", media_type);
		json += "}";
	}
	json += "]";
	return json;
}

} // namespace metaagent::app_host
