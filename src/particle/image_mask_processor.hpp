#pragma once

#include "export.hpp"
#include "media/image.hpp"
#include "particle/pattern_types.hpp"
#include "particle/shape_types.hpp"

namespace metaagent::particle {

struct ImageMaskBuildParams {
	core::String source_image_path;
	int64_t source_file_timestamp = 0;
	int64_t source_file_size = 0;
	ImageSamplingMode image_sampling_mode = ImageSamplingMode::SobelEdges;
	float alpha_threshold = 0.35f;
	float edge_threshold = 0.12f;
	bool use_luminance = true;
	int32_t sample_resolution = 1024;
	float grayscale_gamma = 1.0f;
	float density_grid_scale = 5.0f;
	float target_jitter_normalized = 0.7f;
	int32_t desired_point_count = 0;
};

struct ImageMaskBuildOutput {
	bool success = false;
	core::Array<core::Vec3> local_points_cm;
	core::String debug_info;
};

namespace image_mask {

METAAGENT_API ImageMaskBuildParams make_build_params(
	const core::String& source_image_path,
	const ShapeDefinition& shape_definition,
	int32_t desired_point_count);

METAAGENT_API bool build_mask_from_rgba(
	const RgbaImage& image,
	const ImageMaskBuildParams& params,
	ImageMaskBuildOutput& out_output);

} // namespace image_mask

} // namespace metaagent::particle
