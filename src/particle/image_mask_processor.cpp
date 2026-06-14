#include "particle/image_mask_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <random>
#include <sstream>
#include <vector>

#include "core/math.hpp"

namespace metaagent::particle::image_mask {
namespace {

float luminance01(const core::ColorRGBA& pixel)
{
	return (0.2126f * static_cast<float>(pixel.r)
		+ 0.7152f * static_cast<float>(pixel.g)
		+ 0.0722f * static_cast<float>(pixel.b))
		/ 255.0f;
}

float alpha01(const core::ColorRGBA& pixel)
{
	return static_cast<float>(pixel.a) / 255.0f;
}

int32_t resolve_effective_sample_resolution(
	const int32_t source_width,
	const int32_t source_height,
	const int32_t requested_resolution)
{
	const int32_t clamped_request = core::math::clamp(requested_resolution, 32, 4096);
	if (source_width <= 0 || source_height <= 0)
	{
		return clamped_request;
	}

	const int32_t max_dim = std::max(source_width, source_height);
	return std::min(clamped_request, max_dim);
}

void resolve_mask_dimensions(
	const int32_t source_width,
	const int32_t source_height,
	const int32_t target_resolution,
	int32_t& out_width,
	int32_t& out_height)
{
	const int32_t clamped_resolution = core::math::clamp(target_resolution, 32, 4096);
	out_width = clamped_resolution;
	out_height = clamped_resolution;

	if (source_width <= 0 || source_height <= 0)
	{
		return;
	}

	const float aspect = static_cast<float>(source_width) / static_cast<float>(source_height);
	if (aspect >= 1.0f)
	{
		out_width = clamped_resolution;
		out_height = std::max(32, static_cast<int32_t>(std::lround(
			static_cast<float>(clamped_resolution) / aspect)));
	}
	else
	{
		out_height = clamped_resolution;
		out_width = std::max(32, static_cast<int32_t>(std::lround(
			static_cast<float>(clamped_resolution) * aspect)));
	}
}

const core::ColorRGBA& sample_rgba_nearest(
	const core::Array<core::ColorRGBA>& pixels,
	const int32_t width,
	const int32_t height,
	const float u,
	const float v)
{
	const int32_t x = core::math::clamp(
		static_cast<int32_t>(std::floor(u * static_cast<float>(width - 1))),
		0,
		width - 1);
	const int32_t y = core::math::clamp(
		static_cast<int32_t>(std::floor(v * static_cast<float>(height - 1))),
		0,
		height - 1);
	return pixels[static_cast<size_t>(y * width + x)];
}

void build_downsampled_grayscale(
	const RgbaImage& image,
	const int32_t target_resolution,
	core::Array<float>& out_gray,
	int32_t& out_width,
	int32_t& out_height)
{
	const int32_t effective_resolution = resolve_effective_sample_resolution(
		image.width,
		image.height,
		target_resolution);
	resolve_mask_dimensions(image.width, image.height, effective_resolution, out_width, out_height);

	out_gray.clear();
	if (image.width <= 0 || image.height <= 0 || out_width <= 0 || out_height <= 0)
	{
		return;
	}

	if (out_width == image.width && out_height == image.height)
	{
		out_gray.resize(static_cast<size_t>(image.width * image.height));
		for (size_t index = 0; index < image.pixels.size(); ++index)
		{
			out_gray[index] = luminance01(image.pixels[index]);
		}
		return;
	}

	out_gray.resize(static_cast<size_t>(out_width * out_height));
	for (int32_t y = 0; y < out_height; ++y)
	{
		for (int32_t x = 0; x < out_width; ++x)
		{
			const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(out_width);
			const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(out_height);
			out_gray[static_cast<size_t>(y * out_width + x)] =
				luminance01(sample_rgba_nearest(image.pixels, image.width, image.height, u, v));
		}
	}
}

float sample_gray_at(
	const core::Array<float>& gray,
	const int32_t width,
	const int32_t height,
	const int32_t x,
	const int32_t y)
{
	const int32_t cx = core::math::clamp(x, 0, width - 1);
	const int32_t cy = core::math::clamp(y, 0, height - 1);
	return gray[static_cast<size_t>(cy * width + cx)];
}

void compute_sobel_magnitudes(
	const core::Array<float>& gray,
	const int32_t width,
	const int32_t height,
	core::Array<float>& out_magnitudes)
{
	out_magnitudes.assign(static_cast<size_t>(width * height), 0.0f);
	if (width < 3 || height < 3)
	{
		return;
	}

	for (int32_t y = 1; y < height - 1; ++y)
	{
		for (int32_t x = 1; x < width - 1; ++x)
		{
			const float gx =
				-1.0f * sample_gray_at(gray, width, height, x - 1, y - 1)
				+ 1.0f * sample_gray_at(gray, width, height, x + 1, y - 1)
				- 2.0f * sample_gray_at(gray, width, height, x - 1, y)
				+ 2.0f * sample_gray_at(gray, width, height, x + 1, y)
				- 1.0f * sample_gray_at(gray, width, height, x - 1, y + 1)
				+ 1.0f * sample_gray_at(gray, width, height, x + 1, y + 1);

			const float gy =
				-1.0f * sample_gray_at(gray, width, height, x - 1, y - 1)
				- 2.0f * sample_gray_at(gray, width, height, x, y - 1)
				- 1.0f * sample_gray_at(gray, width, height, x + 1, y - 1)
				+ 1.0f * sample_gray_at(gray, width, height, x - 1, y + 1)
				+ 2.0f * sample_gray_at(gray, width, height, x, y + 1)
				+ 1.0f * sample_gray_at(gray, width, height, x + 1, y + 1);

			const float magnitude = std::sqrt(gx * gx + gy * gy);
			out_magnitudes[static_cast<size_t>(y * width + x)] =
				core::math::clamp(magnitude / 4.0f, 0.0f, 1.0f);
		}
	}
}

float resolve_adaptive_edge_threshold(
	const core::Array<float>& magnitudes,
	const float requested_threshold,
	const int32_t desired_candidate_count)
{
	std::array<int32_t, 256> histogram {};
	int32_t non_zero_count = 0;
	for (const float magnitude : magnitudes)
	{
		if (magnitude <= core::math::k_epsilon)
		{
			continue;
		}

		const int32_t bin = core::math::clamp(
			static_cast<int32_t>(std::floor(magnitude * 255.0f)),
			0,
			255);
		histogram[static_cast<size_t>(bin)]++;
		++non_zero_count;
	}

	if (non_zero_count <= 0)
	{
		return requested_threshold;
	}

	const int32_t target_keep = core::math::clamp(desired_candidate_count, 1, non_zero_count);
	int32_t accumulated = 0;
	float adaptive_threshold = 0.0f;
	for (int32_t bin = 255; bin >= 0; --bin)
	{
		accumulated += histogram[static_cast<size_t>(bin)];
		if (accumulated >= target_keep)
		{
			adaptive_threshold = static_cast<float>(bin) / 255.0f;
			break;
		}
	}

	return std::min(requested_threshold, std::max(adaptive_threshold, core::math::k_epsilon));
}

uint32_t hash_combine32(const uint32_t seed, const uint32_t value)
{
	return seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u));
}

uint32_t float_bits(const float value)
{
	uint32_t bits = 0;
	static_assert(sizeof(float) == sizeof(uint32_t), "float and uint32_t size mismatch");
	std::memcpy(&bits, &value, sizeof(float));
	return bits;
}

float random_01(std::mt19937& rng)
{
	static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	return dist(rng);
}

core::Vec2 apply_cell_jitter(
	const core::Vec2& normalized_point,
	const int32_t grid_size,
	const float jitter_normalized)
{
	if (jitter_normalized <= core::math::k_epsilon || grid_size <= 0)
	{
		return normalized_point;
	}

	const float cell_size = 1.0f / static_cast<float>(grid_size);
	const float jitter_range = cell_size * jitter_normalized;

	uint32_t seed = 2166136261u;
	seed = hash_combine32(seed, float_bits(normalized_point.x));
	seed = hash_combine32(seed, float_bits(normalized_point.y));
	seed = hash_combine32(seed, static_cast<uint32_t>(grid_size));

	std::mt19937 rng(seed);
	const float jitter_x = (random_01(rng) - 0.5f) * jitter_range;
	const float jitter_y = (random_01(rng) - 0.5f) * jitter_range;

	return {
		core::math::clamp(normalized_point.x + jitter_x, 0.0f, 1.0f),
		core::math::clamp(normalized_point.y + jitter_y, 0.0f, 1.0f)};
}

bool is_separated_from_set(
	const core::Vec2& candidate,
	const core::Array<core::Vec2>& existing_points,
	const float min_separation_sq)
{
	for (const core::Vec2& existing : existing_points)
	{
		const float dx = candidate.x - existing.x;
		const float dy = candidate.y - existing.y;
		if ((dx * dx + dy * dy) < min_separation_sq)
		{
			return false;
		}
	}
	return true;
}

void fill_points_with_weighted_scatter(
	const core::Array<float>& weights,
	const int32_t mask_width,
	const int32_t mask_height,
	const int32_t desired_count,
	const float jitter_normalized,
	const int32_t grid_size,
	core::Array<core::Vec2>& in_out_points)
{
	if (static_cast<int32_t>(in_out_points.size()) >= desired_count)
	{
		in_out_points.resize(static_cast<size_t>(desired_count));
		return;
	}

	core::Array<int32_t> weighted_indices;
	core::Array<float> weighted_values;
	float total_weight = 0.0f;
	for (int32_t index = 0; index < static_cast<int32_t>(weights.size()); ++index)
	{
		if (weights[static_cast<size_t>(index)] <= core::math::k_epsilon)
		{
			continue;
		}
		weighted_indices.push_back(index);
		weighted_values.push_back(weights[static_cast<size_t>(index)]);
		total_weight += weights[static_cast<size_t>(index)];
	}

	if (weighted_indices.empty() || total_weight <= core::math::k_epsilon)
	{
		return;
	}

	const float min_separation = 0.5f / static_cast<float>(std::max(mask_width, mask_height));
	const float min_separation_sq = min_separation * min_separation;
	std::mt19937 rng(static_cast<uint32_t>(desired_count * 977 + mask_width * 13 + mask_height));

	int32_t safety_counter = 0;
	while (static_cast<int32_t>(in_out_points.size()) < desired_count && safety_counter < desired_count * 64)
	{
		++safety_counter;
		float pick = random_01(rng) * total_weight;
		int32_t chosen_index = weighted_indices.back();
		for (int32_t wi = 0; wi < static_cast<int32_t>(weighted_indices.size()); ++wi)
		{
			pick -= weighted_values[static_cast<size_t>(wi)];
			if (pick <= 0.0f)
			{
				chosen_index = weighted_indices[static_cast<size_t>(wi)];
				break;
			}
		}

		const int32_t x = chosen_index % mask_width;
		const int32_t y = chosen_index / mask_width;
		core::Vec2 candidate(
			(static_cast<float>(x) + random_01(rng)) / static_cast<float>(mask_width),
			(static_cast<float>(y) + random_01(rng)) / static_cast<float>(mask_height));
		candidate = apply_cell_jitter(candidate, grid_size, jitter_normalized);
		if (!is_separated_from_set(candidate, in_out_points, min_separation_sq))
		{
			continue;
		}
		in_out_points.push_back(candidate);
	}

	while (static_cast<int32_t>(in_out_points.size()) < desired_count)
	{
		const int32_t index = weighted_indices[in_out_points.size() % weighted_indices.size()];
		const int32_t x = index % mask_width;
		const int32_t y = index / mask_width;
		core::Vec2 candidate(
			(static_cast<float>(x) + 0.5f) / static_cast<float>(mask_width),
			(static_cast<float>(y) + 0.5f) / static_cast<float>(mask_height));
		candidate = apply_cell_jitter(candidate, grid_size, std::max(jitter_normalized, 0.5f));
		in_out_points.push_back(candidate);
	}
}

bool scatter_stratified_from_mask_weights(
	const core::Array<float>& weights,
	const int32_t mask_width,
	const int32_t mask_height,
	const ImageMaskBuildParams& params,
	core::Array<core::Vec2>& out_normalized_points,
	int32_t& out_strat_grid_size)
{
	out_normalized_points.clear();
	out_strat_grid_size = 1;

	if (weights.empty() || mask_width <= 0 || mask_height <= 0 || params.desired_point_count <= 0)
	{
		return false;
	}

	struct DensityCell
	{
		float weight_sum = 0.0f;
		core::Vec2 weighted_uv_sum;
	};

	const float density_scale = std::max(0.01f, params.density_grid_scale);
	const int32_t grid_size = std::max(
		1,
		static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(params.desired_point_count) * density_scale))));
	out_strat_grid_size = grid_size;

	core::Array<DensityCell> cells(static_cast<size_t>(grid_size * grid_size));
	for (int32_t y = 0; y < mask_height; ++y)
	{
		for (int32_t x = 0; x < mask_width; ++x)
		{
			const int32_t index = y * mask_width + x;
			const float weight = weights[static_cast<size_t>(index)];
			if (weight <= core::math::k_epsilon)
			{
				continue;
			}

			const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(mask_width);
			const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(mask_height);
			const int32_t cell_x = core::math::clamp(
				static_cast<int32_t>(std::floor(u * static_cast<float>(grid_size))),
				0,
				grid_size - 1);
			const int32_t cell_y = core::math::clamp(
				static_cast<int32_t>(std::floor(v * static_cast<float>(grid_size))),
				0,
				grid_size - 1);
			DensityCell& cell = cells[static_cast<size_t>(cell_y * grid_size + cell_x)];
			cell.weight_sum += weight;
			cell.weighted_uv_sum.x += u * weight;
			cell.weighted_uv_sum.y += v * weight;
		}
	}

	struct WeightedCell
	{
		float weight_sum = 0.0f;
		core::Vec2 uv;
	};

	core::Array<WeightedCell> remaining_cells;
	remaining_cells.reserve(cells.size());
	for (const DensityCell& cell : cells)
	{
		if (cell.weight_sum <= core::math::k_epsilon)
		{
			continue;
		}

		WeightedCell weighted;
		weighted.weight_sum = cell.weight_sum;
		weighted.uv = {
			cell.weighted_uv_sum.x / cell.weight_sum,
			cell.weighted_uv_sum.y / cell.weight_sum};
		remaining_cells.push_back(weighted);
	}

	out_normalized_points.reserve(static_cast<size_t>(params.desired_point_count));
	const int32_t primary_count = std::min(params.desired_point_count, static_cast<int32_t>(remaining_cells.size()));
	std::mt19937 primary_rng(static_cast<uint32_t>(
		params.desired_point_count * 131 + grid_size * 17 + mask_width));

	for (int32_t index = 0; index < primary_count && !remaining_cells.empty(); ++index)
	{
		float total_cell_weight = 0.0f;
		for (const WeightedCell& cell : remaining_cells)
		{
			total_cell_weight += cell.weight_sum;
		}

		int32_t chosen = static_cast<int32_t>(remaining_cells.size() - 1);
		if (total_cell_weight > core::math::k_epsilon)
		{
			float pick = random_01(primary_rng) * total_cell_weight;
			for (int32_t cell_index = 0; cell_index < static_cast<int32_t>(remaining_cells.size()); ++cell_index)
			{
				pick -= remaining_cells[static_cast<size_t>(cell_index)].weight_sum;
				if (pick <= 0.0f)
				{
					chosen = cell_index;
					break;
				}
			}
		}

		const core::Vec2 jittered = apply_cell_jitter(
			remaining_cells[static_cast<size_t>(chosen)].uv,
			grid_size,
			params.target_jitter_normalized);
		out_normalized_points.push_back(jittered);
		remaining_cells.erase(remaining_cells.begin() + chosen);
	}

	fill_points_with_weighted_scatter(
		weights,
		mask_width,
		mask_height,
		params.desired_point_count,
		params.target_jitter_normalized,
		grid_size,
		out_normalized_points);

	return !out_normalized_points.empty();
}

void normalized_points_to_local_points(
	const core::Array<core::Vec2>& normalized_points,
	core::Array<core::Vec3>& out_local_points)
{
	out_local_points.clear();
	out_local_points.reserve(normalized_points.size());
	for (const core::Vec2& point : normalized_points)
	{
		out_local_points.push_back({point.x - 0.5f, 0.5f - point.y, 0.0f});
	}
}

bool extract_grayscale_density_points(
	const RgbaImage& image,
	const ImageMaskBuildParams& params,
	core::Array<core::Vec2>& out_normalized_points,
	int32_t& out_mask_width,
	int32_t& out_mask_height,
	int32_t& out_candidate_count,
	int32_t& out_strat_grid_size,
	core::String& out_debug_info)
{
	core::Array<float> gray;
	build_downsampled_grayscale(image, params.sample_resolution, gray, out_mask_width, out_mask_height);
	if (gray.empty() || out_mask_width <= 0 || out_mask_height <= 0)
	{
		out_debug_info = "Failed to build grayscale density field.";
		return false;
	}

	core::Array<float> weights(gray.size(), 0.0f);
	out_candidate_count = 0;
	for (int32_t y = 0; y < out_mask_height; ++y)
	{
		for (int32_t x = 0; x < out_mask_width; ++x)
		{
			const int32_t index = y * out_mask_width + x;
			const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(out_mask_width);
			const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(out_mask_height);

			const float luminance = gray[static_cast<size_t>(index)];
			const float alpha = alpha01(sample_rgba_nearest(image.pixels, image.width, image.height, u, v));
			float concentration = params.use_luminance
				? (1.0f - luminance) * alpha
				: alpha;
			concentration = std::pow(std::max(concentration, 0.0f), params.grayscale_gamma);

			if (concentration < params.alpha_threshold)
			{
				continue;
			}

			weights[static_cast<size_t>(index)] = concentration;
			++out_candidate_count;
		}
	}

	if (out_candidate_count <= 0)
	{
		out_debug_info = "No grayscale density pixels passed alpha threshold.";
		return false;
	}

	if (!scatter_stratified_from_mask_weights(
			weights,
			out_mask_width,
			out_mask_height,
			params,
			out_normalized_points,
			out_strat_grid_size))
	{
		out_debug_info = "Grayscale density produced no scatter points.";
		return false;
	}

	return true;
}

bool extract_sobel_edge_points(
	const RgbaImage& image,
	const ImageMaskBuildParams& params,
	core::Array<core::Vec2>& out_normalized_points,
	int32_t& out_mask_width,
	int32_t& out_mask_height,
	int32_t& out_candidate_count,
	int32_t& out_strat_grid_size,
	float& out_applied_threshold,
	core::String& out_debug_info)
{
	core::Array<float> gray;
	build_downsampled_grayscale(image, params.sample_resolution, gray, out_mask_width, out_mask_height);
	if (gray.empty())
	{
		out_debug_info = "Failed to build grayscale field.";
		return false;
	}

	core::Array<float> sobel_magnitudes;
	compute_sobel_magnitudes(gray, out_mask_width, out_mask_height, sobel_magnitudes);
	const int32_t desired_candidates = std::max(
		static_cast<int32_t>(std::lround(
			static_cast<float>(params.desired_point_count) * params.density_grid_scale * 3.0f)),
		128);
	out_applied_threshold = resolve_adaptive_edge_threshold(
		sobel_magnitudes,
		params.edge_threshold,
		desired_candidates);

	core::Array<float> weights(sobel_magnitudes.size(), 0.0f);
	out_candidate_count = 0;
	for (size_t index = 0; index < sobel_magnitudes.size(); ++index)
	{
		const float edge_mag = sobel_magnitudes[index];
		if (edge_mag < out_applied_threshold)
		{
			continue;
		}

		weights[index] = std::pow(edge_mag, params.grayscale_gamma);
		++out_candidate_count;
	}

	if (out_candidate_count <= 0)
	{
		out_debug_info = "No Sobel edge pixels passed threshold.";
		return false;
	}

	if (!scatter_stratified_from_mask_weights(
			weights,
			out_mask_width,
			out_mask_height,
			params,
			out_normalized_points,
			out_strat_grid_size))
	{
		out_debug_info = "Sobel edges produced no scatter points.";
		return false;
	}

	return true;
}

} // namespace

ImageMaskBuildParams make_build_params(
	const core::String& source_image_path,
	const ShapeDefinition& shape_definition,
	const int32_t desired_point_count)
{
	ImageMaskBuildParams params;
	params.source_image_path = source_image_path;
	params.image_sampling_mode = shape_definition.image_sampling_mode;
	params.alpha_threshold = shape_definition.alpha_threshold;
	params.edge_threshold = shape_definition.edge_threshold;
	params.use_luminance = shape_definition.use_luminance;
	params.sample_resolution = shape_definition.sample_resolution;
	params.grayscale_gamma = shape_definition.grayscale_gamma;
	params.density_grid_scale = shape_definition.density_grid_scale;
	params.target_jitter_normalized = shape_definition.target_jitter_normalized;
	params.desired_point_count = std::max(1, desired_point_count);

	if (!source_image_path.empty())
	{
		std::error_code ec;
		const std::filesystem::path source_path(source_image_path);
		if (std::filesystem::exists(source_path, ec))
		{
			const auto file_size = std::filesystem::file_size(source_path, ec);
			if (!ec)
			{
				params.source_file_size = static_cast<int64_t>(file_size);
			}

			const auto last_write = std::filesystem::last_write_time(source_path, ec);
			if (!ec)
			{
				params.source_file_timestamp = static_cast<int64_t>(
					last_write.time_since_epoch().count());
			}
		}
	}

	return params;
}

bool build_mask_from_rgba(
	const RgbaImage& image,
	const ImageMaskBuildParams& params,
	ImageMaskBuildOutput& out_output)
{
	out_output = {};
	if (image.width <= 0 || image.height <= 0)
	{
		out_output.debug_info = "Invalid source image dimensions.";
		return false;
	}

	const size_t expected_pixels = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
	if (image.pixels.size() < expected_pixels)
	{
		out_output.debug_info = "RGBA pixel buffer smaller than width*height.";
		return false;
	}

	core::Array<core::Vec2> normalized_points;
	int32_t mask_width = 0;
	int32_t mask_height = 0;
	int32_t candidate_count = 0;
	int32_t strat_grid_size = 1;
	float applied_edge_threshold = params.edge_threshold;

	const ImageSamplingMode mode = params.image_sampling_mode;
	bool extracted = false;
	if (mode == ImageSamplingMode::GrayscaleDensity)
	{
		extracted = extract_grayscale_density_points(
			image,
			params,
			normalized_points,
			mask_width,
			mask_height,
			candidate_count,
			strat_grid_size,
			out_output.debug_info);
	}
	else if (mode == ImageSamplingMode::SobelEdges)
	{
		extracted = extract_sobel_edge_points(
			image,
			params,
			normalized_points,
			mask_width,
			mask_height,
			candidate_count,
			strat_grid_size,
			applied_edge_threshold,
			out_output.debug_info);
	}
	else
	{
		extracted = extract_grayscale_density_points(
			image,
			params,
			normalized_points,
			mask_width,
			mask_height,
			candidate_count,
			strat_grid_size,
			out_output.debug_info);
	}

	if (!extracted)
	{
		return false;
	}

	normalized_points_to_local_points(normalized_points, out_output.local_points_cm);
	out_output.success = !out_output.local_points_cm.empty();

	std::ostringstream stream;
	if (mode == ImageSamplingMode::SobelEdges)
	{
		stream
			<< "SobelEdges src="
			<< image.width
			<< "x"
			<< image.height
			<< " mask="
			<< mask_width
			<< "x"
			<< mask_height
			<< " edgeCandidates="
			<< candidate_count
			<< " points="
			<< out_output.local_points_cm.size()
			<< " stratGrid="
			<< strat_grid_size
			<< "x"
			<< strat_grid_size
			<< " edgeThreshold="
			<< applied_edge_threshold
			<< " gridScale="
			<< params.density_grid_scale
			<< " jitter="
			<< params.target_jitter_normalized;
	}
	else
	{
		stream
			<< "GrayscaleDensity src="
			<< image.width
			<< "x"
			<< image.height
			<< " mask="
			<< mask_width
			<< "x"
			<< mask_height
			<< " weightedPixels="
			<< candidate_count
			<< " points="
			<< out_output.local_points_cm.size()
			<< " stratGrid="
			<< strat_grid_size
			<< "x"
			<< strat_grid_size
			<< " gamma="
			<< params.grayscale_gamma
			<< " gridScale="
			<< params.density_grid_scale
			<< " jitter="
			<< params.target_jitter_normalized;
	}
	out_output.debug_info = stream.str();
	return out_output.success;
}

} // namespace metaagent::particle::image_mask
