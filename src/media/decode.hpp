#pragma once

#include "export.hpp"
#include "media/image.hpp"

namespace droidcli::media {

DROIDCLI_API bool get_file_identity(
	const core::String& path,
	int64_t& out_file_size,
	int64_t& out_modified_unix);

DROIDCLI_API bool decode_image_from_file(const core::String& path, RgbaImage& out_image);

DROIDCLI_API bool decode_image_from_memory(
	const uint8_t* data,
	size_t size,
	RgbaImage& out_image,
	const core::String& source_label = {});

} // namespace droidcli::media
