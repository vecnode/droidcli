#include "media/store.hpp"

#include "media/decode.hpp"

namespace droidcli::media {

MediaStore& MediaStore::instance()
{
	static MediaStore store;
	return store;
}

bool MediaStore::load_file(const core::String& path, RgbaImage& out_image, const bool force_reload)
{
	if (path.empty())
	{
		return false;
	}

	int64_t file_size = 0;
	int64_t modified_unix = 0;
	if (!get_file_identity(path, file_size, modified_unix))
	{
		return false;
	}

	if (!force_reload)
	{
		for (size_t index = 0; index < cache_paths_.size(); ++index)
		{
			if (cache_paths_[index] != path)
			{
				continue;
			}

			const CacheEntry& entry = cache_entries_[index];
			if (entry.cached_file_size == file_size && entry.cached_modified_unix == modified_unix)
			{
				out_image = entry.image;
				return out_image.valid();
			}
			break;
		}
	}

	RgbaImage decoded;
	if (!decode_image_from_file(path, decoded))
	{
		return false;
	}

	CacheEntry entry;
	entry.image = decoded;
	entry.cached_file_size = file_size;
	entry.cached_modified_unix = modified_unix;

	bool replaced = false;
	for (size_t index = 0; index < cache_paths_.size(); ++index)
	{
		if (cache_paths_[index] == path)
		{
			cache_entries_[index] = entry;
			replaced = true;
			break;
		}
	}

	if (!replaced)
	{
		cache_paths_.push_back(path);
		cache_entries_.push_back(entry);
	}

	out_image = decoded;
	return true;
}

const RgbaImage* MediaStore::find_cached(const core::String& path) const
{
	for (size_t index = 0; index < cache_paths_.size(); ++index)
	{
		if (cache_paths_[index] == path)
		{
			return &cache_entries_[index].image;
		}
	}
	return nullptr;
}

void MediaStore::invalidate_path(const core::String& path)
{
	for (size_t index = 0; index < cache_paths_.size(); ++index)
	{
		if (cache_paths_[index] == path)
		{
			cache_paths_.erase(cache_paths_.begin() + static_cast<std::ptrdiff_t>(index));
			cache_entries_.erase(cache_entries_.begin() + static_cast<std::ptrdiff_t>(index));
			return;
		}
	}
}

void MediaStore::invalidate_all()
{
	cache_paths_.clear();
	cache_entries_.clear();
}

} // namespace droidcli::media
