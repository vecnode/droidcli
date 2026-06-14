#include "media/pipeline.hpp"

#include "core/math.hpp"
#include "media/decode.hpp"
#include "media/store.hpp"

#include <cmath>

namespace metaagent::media {
namespace {

uint8_t sample_luminance(const RgbaImage& image, int32_t x, int32_t y)
{
	const size_t index = static_cast<size_t>(y * image.width + x);
	const core::ColorRGBA& color = image.pixels[index];
	return static_cast<uint8_t>(core::math::clamp(
		0.299f * color.r + 0.587f * color.g + 0.114f * color.b,
		0.0f,
		255.0f));
}

void resize_gray_nearest(
	const core::Array<uint8_t>& source,
	int32_t source_width,
	int32_t source_height,
	int32_t target_size,
	core::Array<uint8_t>& out_pixels,
	int32_t& out_width,
	int32_t& out_height)
{
	out_width = target_size;
	out_height = target_size;
	out_pixels.resize(static_cast<size_t>(target_size * target_size));

	if (source_width <= 0 || source_height <= 0 || source.empty())
	{
		return;
	}

	for (int32_t y = 0; y < target_size; ++y)
	{
		const int32_t source_y = (y * source_height) / target_size;
		for (int32_t x = 0; x < target_size; ++x)
		{
			const int32_t source_x = (x * source_width) / target_size;
			out_pixels[static_cast<size_t>(y * target_size + x)] =
				source[static_cast<size_t>(source_y * source_width + source_x)];
		}
	}
}

void resize_rgba_nearest(
	const RgbaImage& source,
	int32_t target_size,
	core::Array<core::ColorRGBA>& out_pixels,
	int32_t& out_width,
	int32_t& out_height)
{
	out_width = target_size;
	out_height = target_size;
	out_pixels.resize(static_cast<size_t>(target_size * target_size));

	if (source.width <= 0 || source.height <= 0 || source.pixels.empty())
	{
		return;
	}

	for (int32_t y = 0; y < target_size; ++y)
	{
		const int32_t source_y = (y * source.height) / target_size;
		for (int32_t x = 0; x < target_size; ++x)
		{
			const int32_t source_x = (x * source.width) / target_size;
			out_pixels[static_cast<size_t>(y * target_size + x)] =
				source.pixels[static_cast<size_t>(source_y * source.width + source_x)];
		}
	}
}

void build_full_grayscale(const RgbaImage& image, core::Array<uint8_t>& out_gray)
{
	const size_t count = image.pixels.size();
	out_gray.resize(count);
	for (size_t index = 0; index < count; ++index)
	{
		const core::ColorRGBA& color = image.pixels[index];
		out_gray[index] = static_cast<uint8_t>(core::math::clamp(
			0.299f * color.r + 0.587f * color.g + 0.114f * color.b,
			0.0f,
			255.0f));
	}
}

void build_sobel_preview(
	const core::Array<uint8_t>& gray,
	int32_t width,
	int32_t height,
	core::Array<uint8_t>& out_sobel)
{
	out_sobel.assign(gray.size(), 0);
	if (width <= 2 || height <= 2)
	{
		return;
	}

	for (int32_t y = 1; y < height - 1; ++y)
	{
		for (int32_t x = 1; x < width - 1; ++x)
		{
			const auto at = [&](const int32_t dx, const int32_t dy) -> float
			{
				return gray[static_cast<size_t>((y + dy) * width + (x + dx))];
			};

			const float gx = -at(-1, -1) - 2.0f * at(-1, 0) - at(-1, 1) + at(1, -1) + 2.0f * at(1, 0) + at(1, 1);
			const float gy = -at(-1, -1) - 2.0f * at(0, -1) - at(1, -1) + at(-1, 1) + 2.0f * at(0, 1) + at(1, 1);
			const float magnitude = std::sqrt(gx * gx + gy * gy);
			out_sobel[static_cast<size_t>(y * width + x)] = static_cast<uint8_t>(core::math::clamp(magnitude, 0.0f, 255.0f));
		}
	}
}

} // namespace

bool build_mask_from_file(
	const core::String& source_path,
	const particle::ImageMaskBuildParams& params,
	MaskBuildResult& out_result,
	const bool force_reload_image)
{
	out_result = {};

	RgbaImage image;
	if (!MediaStore::instance().load_file(source_path, image, force_reload_image))
	{
		out_result.mask.debug_info = "Failed to decode image: " + source_path;
		return false;
	}

	particle::ImageMaskBuildParams effective_params = params;
	effective_params.source_image_path = source_path;
	effective_params.source_file_size = image.metadata.file_size;
	effective_params.source_file_timestamp = image.metadata.file_modified_unix;

	const bool built = particle::image_mask::build_mask_from_rgba(image, effective_params, out_result.mask);
	out_result.success = built && out_result.mask.success;
	build_preview_thumbnails(image, 64, out_result.previews);
	return out_result.success;
}

void build_preview_thumbnails(
	const RgbaImage& source,
	const int32_t thumbnail_size,
	MaskPreviewBuffers& out_previews)
{
	resize_rgba_nearest(
		source,
		thumbnail_size,
		out_previews.source_color,
		out_previews.preview_width,
		out_previews.preview_height);

	core::Array<uint8_t> full_gray;
	build_full_grayscale(source, full_gray);
	resize_gray_nearest(
		full_gray,
		source.width,
		source.height,
		thumbnail_size,
		out_previews.source_gray,
		out_previews.preview_width,
		out_previews.preview_height);
	out_previews.gray_preview = out_previews.source_gray;
	build_sobel_preview(
		out_previews.gray_preview,
		out_previews.preview_width,
		out_previews.preview_height,
		out_previews.sobel_preview);
}

} // namespace metaagent::media
