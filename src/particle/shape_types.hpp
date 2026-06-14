#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::particle {

enum class ShapeType : uint8_t {
	SquareGrid = 0,
	ImageSilhouette,
	SplinePath,
	MeshSilhouette
};

enum class ShapeAssignmentMode : uint8_t {
	NearestNeighbor = 0,
	Ordered,
	PolarMatched
};

enum class ShapeAnchor : uint8_t {
	ParticleCentroid = 0,
	PreviewPlane
};

enum class ImageSamplingMode : uint8_t {
	SobelEdges = 0,
	FilledSilhouette,
	GrayscaleDensity
};

struct ShapeFrame {
	core::Vec3 origin;
	core::Rotator orientation;
	core::Vec2 extents_cm {200.0f, 200.0f};
	float z_offset_cm = 2.0f;
};

struct ShapeDefinition {
	ShapeType shape_type = ShapeType::ImageSilhouette;
	ImageSamplingMode image_sampling_mode = ImageSamplingMode::GrayscaleDensity;
	float alpha_threshold = 0.08f;
	float edge_threshold = 0.12f;
	bool use_luminance = true;
	int32_t sample_resolution = 1024;
	float grayscale_gamma = 1.0f;
	float density_grid_scale = 5.0f;
	float target_jitter_normalized = 0.7f;
	bool use_loaded_preview_texture = true;
	ShapeAnchor shape_anchor = ShapeAnchor::ParticleCentroid;
	bool auto_fit_shape_to_particle_sphere = true;
	bool orient_shape_to_view = true;
	float shape_width_cm = 200.0f;
	float shape_height_cm = 0.0f;
	float z_offset_cm = 2.0f;
	ShapeAssignmentMode assignment_mode = ShapeAssignmentMode::PolarMatched;
	core::String shape_source_actor_tag;
	int32_t procedural_sample_count = 64;

	METAAGENT_API core::String get_shape_display_name() const;
	METAAGENT_API static ImageSamplingMode sanitize_image_sampling_mode(ImageSamplingMode mode);
	METAAGENT_API core::String get_image_sampling_display_name() const;
};

struct ShapeContext {
	core::Array<core::Vec3> baseline_world_positions;
	core::String source_image_path;
	bool has_resolved_image = false;
	bool has_view_origin = false;
	core::Vec3 view_origin;
	core::Vec3 preview_plane_origin;
	core::Rotator preview_plane_orientation;
	core::Vec2 preview_plane_extents_cm {200.0f, 200.0f};
	bool has_preview_plane = false;
	int32_t source_texture_width = 0;
	int32_t source_texture_height = 0;
};

struct ShapeBuildResult {
	bool success = false;
	bool awaiting_async_mask = false;
	core::Array<core::Vec3> pattern_world_targets;
	int32_t pattern_columns = 0;
	core::Vec3 pattern_center;
	ShapeType resolved_shape = ShapeType::SquareGrid;
	ShapeFrame shape_frame;
	int32_t shape_point_count = 0;
	core::String debug_info;
};

} // namespace metaagent::particle
