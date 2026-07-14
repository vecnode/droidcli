#pragma once

#include <cstdint>

#include "export.hpp"
#include "media/image.hpp"

namespace droidcli::media {

class MediaStore {
public:
	DROIDCLI_API static MediaStore& instance();

	DROIDCLI_API bool load_file(const core::String& path, RgbaImage& out_image, bool force_reload = false);

	DROIDCLI_API const RgbaImage* find_cached(const core::String& path) const;

	DROIDCLI_API void invalidate_path(const core::String& path);

	DROIDCLI_API void invalidate_all();

private:
	struct CacheEntry {
		RgbaImage image;
		int64_t cached_file_size = 0;
		int64_t cached_modified_unix = 0;
	};

	core::Array<core::String> cache_paths_;
	core::Array<CacheEntry> cache_entries_;
};

} // namespace droidcli::media
