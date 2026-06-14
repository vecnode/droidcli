#pragma once

#include <cstdint>

#include "export.hpp"
#include "media/image.hpp"

namespace metaagent::media {

class MediaStore {
public:
	METAAGENT_API static MediaStore& instance();

	METAAGENT_API bool load_file(const core::String& path, RgbaImage& out_image, bool force_reload = false);

	METAAGENT_API const RgbaImage* find_cached(const core::String& path) const;

	METAAGENT_API void invalidate_path(const core::String& path);

	METAAGENT_API void invalidate_all();

private:
	struct CacheEntry {
		RgbaImage image;
		int64_t cached_file_size = 0;
		int64_t cached_modified_unix = 0;
	};

	core::Array<core::String> cache_paths_;
	core::Array<CacheEntry> cache_entries_;
};

} // namespace metaagent::media
