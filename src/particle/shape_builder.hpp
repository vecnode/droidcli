#pragma once

#include "export.hpp"
#include "particle/image_mask_processor.hpp"
#include "particle/pattern_types.hpp"
#include "particle/shape_types.hpp"

namespace metaagent::particle {

class ShapeBuilder {
public:
	METAAGENT_API static ShapeBuildResult build_pattern_targets(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context);

	METAAGENT_API static bool build_square_grid_targets(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context,
		ShapeBuildResult& out_result);

	METAAGENT_API static bool build_image_silhouette_targets(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context,
		const RgbaImage* source_image,
		ShapeBuildResult& out_result);

	/** Build targets from pre-extracted normalized local shape points (async mask cache path). */
	METAAGENT_API static bool build_silhouette_from_local_points(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context,
		const core::Array<core::Vec3>& local_shape_points_cm,
		int32_t source_texture_width,
		int32_t source_texture_height,
		ShapeBuildResult& out_result,
		const core::String& extraction_debug = {});

	/** Build targets from world-space polyline samples (UE feeds spline points). */
	METAAGENT_API static bool build_polyline_path_targets(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context,
		const core::Array<core::Vec3>& polyline_world_points,
		ShapeBuildResult& out_result);

	/** Build targets from axis-aligned bounds grid in normalized local space (UE feeds mesh bounds). */
	METAAGENT_API static bool build_bounds_grid_targets(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context,
		const core::Vec3& bounds_origin,
		float bounds_width_cm,
		float bounds_height_cm,
		ShapeBuildResult& out_result);

	METAAGENT_API static ShapeFrame resolve_shape_frame(
		const PatternConfig& pattern_config,
		const ShapeContext& shape_context,
		int32_t source_texture_width,
		int32_t source_texture_height);

	METAAGENT_API static void assign_particles_to_shape_points(
		const core::Array<core::Vec3>& baseline_world_positions,
		const core::Array<core::Vec3>& local_shape_points_cm,
		const ShapeFrame& shape_frame,
		ShapeAssignmentMode assignment_mode,
		core::Array<core::Vec3>& out_world_targets);

	METAAGENT_API static core::Vec3 local_point_to_world(
		const core::Vec3& local_point_cm,
		const ShapeFrame& shape_frame);

	METAAGENT_API static ShapeFrame build_shape_frame_from_centroid(
		const core::Array<core::Vec3>& baseline_world_positions,
		float shape_width_cm,
		float shape_height_cm,
		float z_offset_cm,
		bool auto_fit_to_particle_sphere,
		bool orient_shape_to_view,
		bool has_view_origin,
		const core::Vec3& view_origin);
};

} // namespace metaagent::particle
