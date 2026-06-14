#include "particle/shape_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

#include "core/math.hpp"
#include "particle/image_mask_processor.hpp"

namespace metaagent::particle {
namespace {

using core::Vec3;

float distance_squared(const Vec3& a, const Vec3& b)
{
	const Vec3 delta = a - b;
	return delta.length_squared();
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x};
}

core::Rotator rotator_from_basis(const Vec3& x_axis, const Vec3& y_axis, const Vec3& z_axis)
{
	const float m00 = x_axis.x;
	const float m10 = x_axis.y;
	const float m20 = x_axis.z;

	const float m01 = y_axis.x;
	const float m11 = y_axis.y;
	const float m21 = y_axis.z;

	const float m22 = z_axis.z;

	const float pitch_rad = std::asin(core::math::clamp(-m20, -1.0f, 1.0f));
	const float cp = std::cos(pitch_rad);
	float yaw_rad = 0.0f;
	float roll_rad = 0.0f;

	if (std::abs(cp) > 1e-5f)
	{
		yaw_rad = std::atan2(m10, m00);
		roll_rad = std::atan2(m21, m22);
	}
	else
	{
		yaw_rad = std::atan2(-m01, m11);
	}

	const float rad_to_deg = 180.0f / core::math::k_pi;
	return {
		pitch_rad * rad_to_deg,
		yaw_rad * rad_to_deg,
		roll_rad * rad_to_deg};
}

Vec3 compute_centroid(const core::Array<Vec3>& points)
{
	if (points.empty())
	{
		return {};
	}

	Vec3 centroid;
	for (const Vec3& point : points)
	{
		centroid += point;
	}
	const float inv_count = 1.0f / static_cast<float>(points.size());
	return centroid * inv_count;
}

bool finalize_shape_assignment(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	const core::Array<Vec3>& local_shape_points_cm,
	const ShapeFrame shape_frame,
	const ShapeType resolved_shape,
	const core::String& debug_info,
	ShapeBuildResult& out_result)
{
	const core::Array<Vec3>& baseline = shape_context.baseline_world_positions;
	const int32_t particle_count = static_cast<int32_t>(baseline.size());
	if (particle_count <= 0 || local_shape_points_cm.empty())
	{
		out_result.success = false;
		out_result.debug_info = debug_info.empty() ? "No shape points to assign." : debug_info;
		return false;
	}

	ShapeBuilder::assign_particles_to_shape_points(
		baseline,
		local_shape_points_cm,
		shape_frame,
		pattern_config.shape.assignment_mode,
		out_result.pattern_world_targets);

	out_result.shape_frame = shape_frame;
	out_result.pattern_center = shape_frame.origin;
	out_result.pattern_columns = std::max(
		1,
		static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(particle_count)))));
	out_result.shape_point_count = static_cast<int32_t>(local_shape_points_cm.size());
	out_result.resolved_shape = resolved_shape;
	out_result.success = true;
	out_result.debug_info = debug_info;
	return true;
}

core::Array<Vec3> world_polyline_to_local_points(
	const core::Array<Vec3>& polyline_world_points,
	const Vec3& centroid,
	const float extent_cm)
{
	const float safe_extent = std::max(10.0f, extent_cm);
	core::Array<Vec3> local_points;
	local_points.reserve(polyline_world_points.size());
	for (const Vec3& world_point : polyline_world_points)
	{
		const Vec3 delta = world_point - centroid;
		local_points.push_back({delta.x / safe_extent, delta.y / safe_extent, 0.0f});
	}
	return local_points;
}

core::Array<Vec3> build_normalized_bounds_grid(const int32_t grid_side)
{
	const int32_t safe_grid_side = core::math::clamp(grid_side, 2, 32);
	core::Array<Vec3> local_points;
	local_points.reserve(static_cast<size_t>(safe_grid_side * safe_grid_side));
	for (int32_t row = 0; row < safe_grid_side; ++row)
	{
		for (int32_t column = 0; column < safe_grid_side; ++column)
		{
			const float u = safe_grid_side > 1
				? static_cast<float>(column) / static_cast<float>(safe_grid_side - 1)
				: 0.5f;
			const float v = safe_grid_side > 1
				? static_cast<float>(row) / static_cast<float>(safe_grid_side - 1)
				: 0.5f;
			local_points.push_back({u - 0.5f, 0.5f - v, 0.0f});
		}
	}
	return local_points;
}

} // namespace

bool ShapeBuilder::build_silhouette_from_local_points(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	const core::Array<Vec3>& local_shape_points_cm,
	const int32_t source_texture_width,
	const int32_t source_texture_height,
	ShapeBuildResult& out_result,
	const core::String& extraction_debug)
{
	const int32_t particle_count = static_cast<int32_t>(shape_context.baseline_world_positions.size());
	if (particle_count <= 0)
	{
		out_result.success = false;
		out_result.debug_info = "No baseline particles.";
		return false;
	}

	if (local_shape_points_cm.empty())
	{
		out_result.success = false;
		out_result.debug_info = extraction_debug.empty()
			? "No silhouette local points."
			: extraction_debug;
		return false;
	}

	const ShapeFrame shape_frame = resolve_shape_frame(
		pattern_config,
		shape_context,
		source_texture_width,
		source_texture_height);

	std::ostringstream stream;
	if (!extraction_debug.empty())
	{
		stream << extraction_debug << " | ";
	}
	stream
		<< "frame="
		<< shape_frame.extents_cm.x
		<< "x"
		<< shape_frame.extents_cm.y
		<< "cm anchor="
		<< (pattern_config.shape.shape_anchor == ShapeAnchor::PreviewPlane
				? "PreviewPlane"
				: "ParticleCentroid")
		<< " autoFit="
		<< (pattern_config.shape.auto_fit_shape_to_particle_sphere ? "on" : "off");

	return finalize_shape_assignment(
		pattern_config,
		shape_context,
		local_shape_points_cm,
		shape_frame,
		ShapeType::ImageSilhouette,
		stream.str(),
		out_result);
}

bool ShapeBuilder::build_polyline_path_targets(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	const core::Array<Vec3>& polyline_world_points,
	ShapeBuildResult& out_result)
{
	const core::Array<Vec3>& baseline = shape_context.baseline_world_positions;
	const int32_t particle_count = static_cast<int32_t>(baseline.size());
	if (particle_count <= 0)
	{
		out_result.success = false;
		out_result.debug_info = "No baseline particles for polyline path.";
		return false;
	}

	if (polyline_world_points.empty())
	{
		out_result.success = false;
		out_result.debug_info = "Polyline path requires at least one world sample.";
		return false;
	}

	const Vec3 centroid = compute_centroid(baseline);
	const core::Array<Vec3> local_shape_points_cm = world_polyline_to_local_points(
		polyline_world_points,
		centroid,
		pattern_config.shape.shape_width_cm);
	const ShapeFrame shape_frame = resolve_shape_frame(pattern_config, shape_context, 0, 0);

	std::ostringstream stream;
	stream << "SplinePath samples=" << polyline_world_points.size();
	return finalize_shape_assignment(
		pattern_config,
		shape_context,
		local_shape_points_cm,
		shape_frame,
		ShapeType::SplinePath,
		stream.str(),
		out_result);
}

bool ShapeBuilder::build_bounds_grid_targets(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	const Vec3& bounds_origin,
	const float bounds_width_cm,
	const float bounds_height_cm,
	ShapeBuildResult& out_result)
{
	const core::Array<Vec3>& baseline = shape_context.baseline_world_positions;
	const int32_t particle_count = static_cast<int32_t>(baseline.size());
	if (particle_count <= 0)
	{
		out_result.success = false;
		out_result.debug_info = "No baseline particles for bounds grid.";
		return false;
	}

	const int32_t grid_side = core::math::clamp(
		static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(std::max(
			particle_count,
			pattern_config.shape.procedural_sample_count))))),
		2,
		32);
	const core::Array<Vec3> local_shape_points_cm = build_normalized_bounds_grid(grid_side);

	ShapeFrame shape_frame = build_shape_frame_from_centroid(
		baseline,
		bounds_width_cm,
		bounds_height_cm,
		pattern_config.shape.z_offset_cm,
		false,
		pattern_config.shape.orient_shape_to_view,
		shape_context.has_view_origin,
		shape_context.view_origin);
	shape_frame.origin = bounds_origin;

	std::ostringstream stream;
	stream << "MeshSilhouette grid=" << grid_side << "x" << grid_side;
	return finalize_shape_assignment(
		pattern_config,
		shape_context,
		local_shape_points_cm,
		shape_frame,
		ShapeType::MeshSilhouette,
		stream.str(),
		out_result);
}

ShapeBuildResult ShapeBuilder::build_pattern_targets(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context)
{
	ShapeBuildResult result;
	result.resolved_shape = pattern_config.shape.shape_type;

	if (pattern_config.shape.shape_type == ShapeType::ImageSilhouette
		&& shape_context.has_resolved_image
		&& !shape_context.source_image_path.empty())
	{
		const core::String failure_reason =
			"ImageSilhouette requested but RGBA source image was not provided.";
		result.resolved_shape = ShapeType::SquareGrid;
		build_square_grid_targets(pattern_config, shape_context, result);
		result.debug_info = "Fallback SquareGrid | " + failure_reason;
		return result;
	}

	build_square_grid_targets(pattern_config, shape_context, result);
	return result;
}

bool ShapeBuilder::build_square_grid_targets(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	ShapeBuildResult& out_result)
{
	const core::Array<Vec3>& baseline = shape_context.baseline_world_positions;
	const int32_t particle_count = static_cast<int32_t>(baseline.size());
	out_result.pattern_world_targets.clear();

	if (particle_count <= 0)
	{
		out_result.success = false;
		out_result.debug_info = "No baseline particles.";
		return false;
	}

	const int32_t pattern_columns = std::max(
		1,
		static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(particle_count)))));
	const int32_t row_count = (particle_count + pattern_columns - 1) / pattern_columns;

	const Vec3 centroid = compute_centroid(baseline);
	const float grid_spacing_cm = std::max(1.0f, pattern_config.grid_spacing_cm);
	const Vec3 grid_origin = centroid - Vec3(
		(pattern_columns - 1) * grid_spacing_cm * 0.5f,
		(row_count - 1) * grid_spacing_cm * 0.5f,
		0.0f);

	out_result.pattern_world_targets.resize(static_cast<size_t>(particle_count));
	for (int32_t particle_index = 0; particle_index < particle_count; ++particle_index)
	{
		const int32_t column = particle_index % pattern_columns;
		const int32_t row = particle_index / pattern_columns;
		out_result.pattern_world_targets[static_cast<size_t>(particle_index)] = grid_origin + Vec3(
			column * grid_spacing_cm,
			row * grid_spacing_cm,
			0.0f);
	}

	out_result.shape_frame = build_shape_frame_from_centroid(
		baseline,
		static_cast<float>(pattern_columns) * grid_spacing_cm,
		static_cast<float>(row_count) * grid_spacing_cm,
		pattern_config.shape.z_offset_cm,
		false,
		false,
		false,
		{});
	out_result.shape_frame.origin = centroid;
	out_result.pattern_center = centroid;
	out_result.pattern_columns = pattern_columns;
	out_result.shape_point_count = particle_count;
	out_result.resolved_shape = ShapeType::SquareGrid;
	out_result.success = true;

	std::ostringstream stream;
	stream << "SquareGrid columns=" << pattern_columns << " spacing=" << grid_spacing_cm;
	out_result.debug_info = stream.str();
	return true;
}

bool ShapeBuilder::build_image_silhouette_targets(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	const RgbaImage* source_image,
	ShapeBuildResult& out_result)
{
	const core::Array<Vec3>& baseline = shape_context.baseline_world_positions;
	const int32_t particle_count = static_cast<int32_t>(baseline.size());
	if (particle_count <= 0)
	{
		out_result.success = false;
		out_result.debug_info = "No baseline particles.";
		return false;
	}

	if (!source_image || source_image->width <= 0 || source_image->height <= 0)
	{
		out_result.success = false;
		out_result.debug_info = "No source image provided for image silhouette.";
		return false;
	}

	ShapeFrame shape_frame = resolve_shape_frame(
		pattern_config,
		shape_context,
		source_image->width,
		source_image->height);

	const ImageMaskBuildParams params = image_mask::make_build_params(
		shape_context.source_image_path,
		pattern_config.shape,
		particle_count);
	ImageMaskBuildOutput mask_output;
	if (!image_mask::build_mask_from_rgba(*source_image, params, mask_output))
	{
		out_result.success = false;
		out_result.debug_info = mask_output.debug_info.empty()
			? "Image mask extraction failed."
			: mask_output.debug_info;
		return false;
	}

	assign_particles_to_shape_points(
		baseline,
		mask_output.local_points_cm,
		shape_frame,
		pattern_config.shape.assignment_mode,
		out_result.pattern_world_targets);

	out_result.shape_frame = shape_frame;
	out_result.pattern_center = shape_frame.origin;
	out_result.pattern_columns = std::max(
		1,
		static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(particle_count)))));
	out_result.shape_point_count = static_cast<int32_t>(mask_output.local_points_cm.size());
	out_result.resolved_shape = ShapeType::ImageSilhouette;
	out_result.success = true;

	std::ostringstream stream;
	stream
		<< mask_output.debug_info
		<< " | frame="
		<< shape_frame.extents_cm.x
		<< "x"
		<< shape_frame.extents_cm.y
		<< "cm anchor="
		<< (pattern_config.shape.shape_anchor == ShapeAnchor::PreviewPlane
				? "PreviewPlane"
				: "ParticleCentroid")
		<< " autoFit="
		<< (pattern_config.shape.auto_fit_shape_to_particle_sphere ? "on" : "off");
	out_result.debug_info = stream.str();
	return true;
}

ShapeFrame ShapeBuilder::resolve_shape_frame(
	const PatternConfig& pattern_config,
	const ShapeContext& shape_context,
	const int32_t source_texture_width,
	const int32_t source_texture_height)
{
	const ShapeDefinition& shape_def = pattern_config.shape;

	if (shape_def.shape_anchor == ShapeAnchor::PreviewPlane && shape_context.has_preview_plane)
	{
		ShapeFrame frame;
		frame.origin = shape_context.preview_plane_origin;
		frame.orientation = shape_context.preview_plane_orientation;
		frame.z_offset_cm = shape_def.z_offset_cm;
		frame.extents_cm = {
			std::max(10.0f, shape_context.preview_plane_extents_cm.x),
			std::max(10.0f, shape_context.preview_plane_extents_cm.y)};
		return frame;
	}

	float shape_height_cm = shape_def.shape_height_cm;
	if (shape_height_cm <= 0.0f && source_texture_width > 0 && source_texture_height > 0)
	{
		shape_height_cm = shape_def.shape_width_cm
			* (static_cast<float>(source_texture_height) / static_cast<float>(source_texture_width));
	}

	return build_shape_frame_from_centroid(
		shape_context.baseline_world_positions,
		shape_def.shape_width_cm,
		shape_height_cm,
		shape_def.z_offset_cm,
		shape_def.auto_fit_shape_to_particle_sphere,
		shape_def.orient_shape_to_view,
		shape_context.has_view_origin,
		shape_context.view_origin);
}

void ShapeBuilder::assign_particles_to_shape_points(
	const core::Array<Vec3>& baseline_world_positions,
	const core::Array<Vec3>& local_shape_points_cm,
	const ShapeFrame& shape_frame,
	const ShapeAssignmentMode assignment_mode,
	core::Array<Vec3>& out_world_targets)
{
	const int32_t particle_count = static_cast<int32_t>(baseline_world_positions.size());
	out_world_targets.clear();
	if (particle_count <= 0 || local_shape_points_cm.empty())
	{
		return;
	}

	core::Array<Vec3> shape_world_points;
	shape_world_points.reserve(local_shape_points_cm.size());
	for (const Vec3& local_point : local_shape_points_cm)
	{
		shape_world_points.push_back(local_point_to_world(local_point, shape_frame));
	}

	out_world_targets.resize(static_cast<size_t>(particle_count));

	if (assignment_mode == ShapeAssignmentMode::Ordered)
	{
		for (int32_t particle_index = 0; particle_index < particle_count; ++particle_index)
		{
			const int32_t shape_index = particle_index
				% static_cast<int32_t>(shape_world_points.size());
			out_world_targets[static_cast<size_t>(particle_index)] =
				shape_world_points[static_cast<size_t>(shape_index)];
		}
		return;
	}

	if (assignment_mode == ShapeAssignmentMode::PolarMatched)
	{
		const Vec3 baseline_centroid = compute_centroid(baseline_world_positions);
		const Vec3 shape_centroid = compute_centroid(shape_world_points);

		core::Array<int32_t> particle_order(static_cast<size_t>(particle_count));
		std::iota(particle_order.begin(), particle_order.end(), 0);
		std::sort(
			particle_order.begin(),
			particle_order.end(),
			[&](const int32_t a, const int32_t b)
			{
				const Vec3 delta_a = baseline_world_positions[static_cast<size_t>(a)] - baseline_centroid;
				const Vec3 delta_b = baseline_world_positions[static_cast<size_t>(b)] - baseline_centroid;
				return std::atan2(delta_a.y, delta_a.x) < std::atan2(delta_b.y, delta_b.x);
			});

		core::Array<int32_t> shape_order(shape_world_points.size());
		std::iota(shape_order.begin(), shape_order.end(), 0);
		std::sort(
			shape_order.begin(),
			shape_order.end(),
			[&](const int32_t a, const int32_t b)
			{
				const Vec3 delta_a = shape_world_points[static_cast<size_t>(a)] - shape_centroid;
				const Vec3 delta_b = shape_world_points[static_cast<size_t>(b)] - shape_centroid;
				return std::atan2(delta_a.y, delta_a.x) < std::atan2(delta_b.y, delta_b.x);
			});

		for (int32_t order_index = 0; order_index < particle_count; ++order_index)
		{
			const int32_t particle_index = particle_order[static_cast<size_t>(order_index)];
			const int32_t shape_index = shape_order[static_cast<size_t>(
				order_index % static_cast<int32_t>(shape_order.size()))];
			out_world_targets[static_cast<size_t>(particle_index)] =
				shape_world_points[static_cast<size_t>(shape_index)];
		}
		return;
	}

	core::Array<bool> used_shape_points(shape_world_points.size(), false);
	core::Array<int32_t> particle_order(static_cast<size_t>(particle_count));
	std::iota(particle_order.begin(), particle_order.end(), 0);
	std::sort(
		particle_order.begin(),
		particle_order.end(),
		[&](const int32_t a, const int32_t b)
		{
			const Vec3& pos_a = baseline_world_positions[static_cast<size_t>(a)];
			const Vec3& pos_b = baseline_world_positions[static_cast<size_t>(b)];
			if (std::fabs(pos_a.y - pos_b.y) > 0.01f)
			{
				return pos_a.y < pos_b.y;
			}
			return pos_a.x < pos_b.x;
		});

	for (const int32_t particle_index : particle_order)
	{
		const Vec3& baseline_position = baseline_world_positions[static_cast<size_t>(particle_index)];
		float best_distance_sq = std::numeric_limits<float>::max();
		int32_t best_shape_index = -1;

		for (int32_t shape_index = 0; shape_index < static_cast<int32_t>(shape_world_points.size()); ++shape_index)
		{
			if (used_shape_points[static_cast<size_t>(shape_index)])
			{
				continue;
			}

			const float d2 = distance_squared(
				baseline_position,
				shape_world_points[static_cast<size_t>(shape_index)]);
			if (d2 < best_distance_sq)
			{
				best_distance_sq = d2;
				best_shape_index = shape_index;
			}
		}

		if (best_shape_index < 0)
		{
			best_shape_index = particle_index % static_cast<int32_t>(shape_world_points.size());
		}

		used_shape_points[static_cast<size_t>(best_shape_index)] = true;
		out_world_targets[static_cast<size_t>(particle_index)] =
			shape_world_points[static_cast<size_t>(best_shape_index)];
	}
}

Vec3 ShapeBuilder::local_point_to_world(
	const Vec3& local_point_cm,
	const ShapeFrame& shape_frame)
{
	const Vec3 scaled_local(
		local_point_cm.x * shape_frame.extents_cm.x,
		local_point_cm.y * shape_frame.extents_cm.y,
		shape_frame.z_offset_cm);
	return shape_frame.origin + shape_frame.orientation.rotate_vector(scaled_local);
}

ShapeFrame ShapeBuilder::build_shape_frame_from_centroid(
	const core::Array<Vec3>& baseline_world_positions,
	float shape_width_cm,
	float shape_height_cm,
	const float z_offset_cm,
	const bool auto_fit_to_particle_sphere,
	const bool orient_shape_to_view,
	const bool has_view_origin,
	const Vec3& view_origin)
{
	ShapeFrame frame;
	frame.z_offset_cm = z_offset_cm;

	if (baseline_world_positions.empty())
	{
		frame.extents_cm = {
			std::max(10.0f, shape_width_cm),
			std::max(10.0f, shape_height_cm > 0.0f ? shape_height_cm : shape_width_cm)};
		return frame;
	}

	const Vec3 centroid = compute_centroid(baseline_world_positions);
	float bounding_radius_cm = 0.0f;
	for (const Vec3& position : baseline_world_positions)
	{
		const float distance = (position - centroid).length();
		if (distance > bounding_radius_cm)
		{
			bounding_radius_cm = distance;
		}
	}

	float resolved_width_cm = shape_width_cm;
	float resolved_height_cm = shape_height_cm;
	if (auto_fit_to_particle_sphere && bounding_radius_cm > core::math::k_epsilon)
	{
		const float diameter_cm = std::max(10.0f, bounding_radius_cm * 2.0f * 0.92f);
		if (shape_height_cm > 0.0f && shape_width_cm > 0.0f)
		{
			const float aspect = shape_height_cm / shape_width_cm;
			resolved_width_cm = diameter_cm;
			resolved_height_cm = diameter_cm * aspect;
		}
		else
		{
			resolved_width_cm = diameter_cm;
			resolved_height_cm = diameter_cm;
		}
	}
	if (resolved_height_cm <= 0.0f)
	{
		resolved_height_cm = resolved_width_cm;
	}

	frame.origin = centroid;
	frame.extents_cm = {
		std::max(10.0f, resolved_width_cm),
		std::max(10.0f, resolved_height_cm)};
	frame.orientation = {};

	if (orient_shape_to_view && has_view_origin)
	{
		Vec3 plane_normal = (view_origin - centroid).normalized();
		if (!plane_normal.nearly_zero())
		{
			Vec3 up_reference(0.0f, 0.0f, 1.0f);
			if (std::fabs(plane_normal.dot(up_reference)) > 0.95f)
			{
				up_reference = Vec3(1.0f, 0.0f, 0.0f);
			}

			const Vec3 tangent_x = cross(up_reference, plane_normal).normalized(1.0f, 0.0f, 0.0f);
			const Vec3 tangent_y = cross(plane_normal, tangent_x).normalized(0.0f, 1.0f, 0.0f);
			frame.orientation = rotator_from_basis(tangent_x, tangent_y, plane_normal);
		}
	}

	return frame;
}

} // namespace metaagent::particle
