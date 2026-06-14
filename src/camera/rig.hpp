#pragma once

#include "export.hpp"
#include "camera/types.hpp"

namespace metaagent::camera {

METAAGENT_API void sanitize_zoom_settings(ZoomSettings& settings);

METAAGENT_API void apply_zoom_input(ZoomSettings& settings, const ZoomInput& input);

METAAGENT_API void sanitize_cinematic_settings(CinematicSettings& settings);

METAAGENT_API void initialize_cinematic_state_from_pose(
	CinematicRuntimeState& state,
	const CinematicSettings& settings,
	const core::Vec3& current_camera_location,
	const FocusTarget& focus);

METAAGENT_API CameraPose compute_cinematic_pose(
	const CinematicSettings& settings,
	CinematicRuntimeState& state,
	const FocusTarget& focus,
	float delta_time_seconds);

METAAGENT_API FocusTarget make_focus_from_bounds(const Bounds3& bounds, float padding_scale = 1.15f);

METAAGENT_API void apply_orbit_radius_zoom(
	CinematicRuntimeState& state,
	const ZoomInput& input,
	float min_radius,
	float max_radius,
	float wheel_step);

} // namespace metaagent::camera
