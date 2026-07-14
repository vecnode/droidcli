#pragma once

#include "core/types.hpp"

namespace droidcli::media {

struct ImageMetadata {
	core::String source_path;
	int64_t file_size = 0;
	int64_t file_modified_unix = 0;
	int32_t width = 0;
	int32_t height = 0;
	int32_t channel_count = 0;
};

struct RgbaImage {
	int32_t width = 0;
	int32_t height = 0;
	core::Array<core::ColorRGBA> pixels;
	ImageMetadata metadata;

	bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

} // namespace droidcli::media
