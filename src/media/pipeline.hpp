#pragma once

#include "export.hpp"
#include "media/image.hpp"
#include "particle/image_mask_processor.hpp"

namespace metaagent::media {

struct MaskPreviewBuffers {
	int32_t preview_width = 0;
	int32_t preview_height = 0;
	core::Array<core::ColorRGBA> source_color;
	core::Array<uint8_t> source_gray;
	core::Array<uint8_t> gray_preview;
	core::Array<uint8_t> sobel_preview;
};

struct MaskBuildResult {
	bool success = false;
	particle::ImageMaskBuildOutput mask;
	MaskPreviewBuffers previews;
};

METAAGENT_API bool build_mask_from_file(
	const core::String& source_path,
	const particle::ImageMaskBuildParams& params,
	MaskBuildResult& out_result,
	bool force_reload_image = false);

METAAGENT_API void build_preview_thumbnails(
	const RgbaImage& source,
	int32_t thumbnail_size,
	MaskPreviewBuffers& out_previews);

} // namespace metaagent::media
