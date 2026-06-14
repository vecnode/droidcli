#pragma once

#include "core/types.hpp"

namespace metaagent::camera {

enum class CinematicStyle : uint8_t {
	OscillatingHold = 0,
	SlowOrbit = 1
};

struct ZoomSettings {
	float min_distance = 40.0f;
	float max_distance = 1500.0f;
	float mouse_wheel_step = 10.0f;
	float desired_distance = -1.0f;
};

struct ZoomInput {
	float discrete_wheel_delta = 0.0f;
	float analog_wheel_axis = 0.0f;
};

struct CinematicSettings {
	CinematicStyle active_style = CinematicStyle::OscillatingHold;
	float pan_duration_seconds = 4.0f;
	float oscillation_yaw_amplitude_degrees = 0.0f;
	float close_orbit_radius = 105.0f;
	float sway_horizontal_amplitude = 0.0f;
	float sway_vertical_amplitude = 0.0f;
	float sway_frequency = 0.85f;
};

struct CinematicRuntimeState {
	bool mode_enabled = false;
	float pan_elapsed_seconds = 0.0f;
	float orbit_radius = 400.0f;
	float camera_height_offset = 100.0f;
	float start_orbit_yaw_degrees = 0.0f;
	float sway_phase_offset = 0.0f;
};

struct CameraPose {
	core::Vec3 location;
	core::Rotator rotation;
};

struct FocusTarget {
	core::Vec3 focus_point;
	float orbit_radius_hint = 400.0f;
	float height_offset = 100.0f;
};

struct Bounds3 {
	core::Vec3 min;
	core::Vec3 max;
	core::Vec3 center() const;
	float radius_xy() const;
};

METAAGENT_API Bounds3 compute_bounds(const core::Array<core::Vec3>& points);

METAAGENT_API CinematicStyle cycle_cinematic_style(CinematicStyle current);

METAAGENT_API const char* cinematic_style_label(CinematicStyle style);

} // namespace metaagent::camera
