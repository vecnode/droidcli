#include "camera/rig.hpp"

#include "core/math.hpp"

#include <cmath>

namespace metaagent::camera {
namespace {

constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_two_pi = k_pi * 2.0f;

core::Rotator look_at_rotation(const core::Vec3& from, const core::Vec3& to)
{
	const core::Vec3 delta = to - from;
	if (delta.nearly_zero())
	{
		return {};
	}

	const float yaw_rad = std::atan2(delta.y, delta.x);
	const float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
	const float pitch_rad = std::atan2(delta.z, horizontal);

	core::Rotator rotation;
	rotation.yaw_deg = yaw_rad * 180.0f / k_pi;
	rotation.pitch_deg = pitch_rad * 180.0f / k_pi;
	rotation.roll_deg = 0.0f;
	return rotation;
}

} // namespace

void sanitize_zoom_settings(ZoomSettings& settings)
{
	settings.min_distance = std::max(1.0f, settings.min_distance);
	settings.max_distance = std::max(settings.min_distance, settings.max_distance);
	settings.mouse_wheel_step = std::max(0.1f, settings.mouse_wheel_step);
	if (settings.desired_distance < 0.0f)
	{
		settings.desired_distance = settings.max_distance * 0.5f;
	}
	settings.desired_distance = core::math::clamp(
		settings.desired_distance,
		settings.min_distance,
		settings.max_distance);
}

void apply_zoom_input(ZoomSettings& settings, const ZoomInput& input)
{
	sanitize_zoom_settings(settings);
	if (std::fabs(input.discrete_wheel_delta) > 1e-4f)
	{
		settings.desired_distance -= input.discrete_wheel_delta * settings.mouse_wheel_step;
	}
	else if (std::fabs(input.analog_wheel_axis) > 1e-4f)
	{
		settings.desired_distance -= input.analog_wheel_axis * settings.mouse_wheel_step;
	}
	settings.desired_distance = core::math::clamp(
		settings.desired_distance,
		settings.min_distance,
		settings.max_distance);
}

void sanitize_cinematic_settings(CinematicSettings& settings)
{
	settings.pan_duration_seconds = std::max(0.1f, settings.pan_duration_seconds);
	settings.oscillation_yaw_amplitude_degrees = std::max(0.0f, settings.oscillation_yaw_amplitude_degrees);
	settings.close_orbit_radius = std::max(60.0f, settings.close_orbit_radius);
	settings.sway_horizontal_amplitude = std::max(0.0f, settings.sway_horizontal_amplitude);
	settings.sway_vertical_amplitude = std::max(0.0f, settings.sway_vertical_amplitude);
	settings.sway_frequency = std::max(0.1f, settings.sway_frequency);
}

void initialize_cinematic_state_from_pose(
	CinematicRuntimeState& state,
	const CinematicSettings& settings,
	const core::Vec3& current_camera_location,
	const FocusTarget& focus)
{
	CinematicSettings sanitized_settings = settings;
	sanitize_cinematic_settings(sanitized_settings);
	state.mode_enabled = true;
	state.pan_elapsed_seconds = 0.0f;
	state.sway_phase_offset = 0.0f;

	const core::Vec3 to_camera = current_camera_location - focus.focus_point;
	core::Vec3 to_camera_xy = to_camera;
	to_camera_xy.z = 0.0f;

	state.orbit_radius = core::math::clamp(
		to_camera_xy.length(),
		sanitized_settings.close_orbit_radius,
		5000.0f);
	if (to_camera_xy.nearly_zero())
	{
		state.orbit_radius = focus.orbit_radius_hint > 0.0f
			? focus.orbit_radius_hint
			: sanitized_settings.close_orbit_radius;
		state.start_orbit_yaw_degrees = 0.0f;
	}
	else
	{
		state.start_orbit_yaw_degrees = std::atan2(to_camera_xy.y, to_camera_xy.x) * 180.0f / k_pi;
	}

	state.camera_height_offset = core::math::clamp(
		to_camera.z,
		30.0f,
		160.0f);
	if (focus.height_offset > 0.0f)
	{
		state.camera_height_offset = focus.height_offset;
	}
}

CameraPose compute_cinematic_pose(
	const CinematicSettings& settings,
	CinematicRuntimeState& state,
	const FocusTarget& focus,
	const float delta_time_seconds)
{
	CameraPose pose;
	if (!state.mode_enabled || delta_time_seconds <= 0.0f)
	{
		return pose;
	}

	CinematicSettings sanitized_settings = settings;
	sanitize_cinematic_settings(sanitized_settings);
	state.pan_elapsed_seconds += delta_time_seconds;

	const float duration = std::max(0.1f, sanitized_settings.pan_duration_seconds);
	float style_time_scale = 0.1f;
	float current_orbit_yaw = state.start_orbit_yaw_degrees;

	switch (sanitized_settings.active_style)
	{
	case CinematicStyle::SlowOrbit:
	{
		const float orbit_speed_degrees_per_second = 14.0f * style_time_scale;
		current_orbit_yaw = state.start_orbit_yaw_degrees
			+ (state.pan_elapsed_seconds * orbit_speed_degrees_per_second);
		break;
	}
	case CinematicStyle::OscillatingHold:
	default:
	{
		const float oscillation_frequency_radians = ((k_two_pi / duration) * style_time_scale);
		const float yaw_offset = std::sin(state.pan_elapsed_seconds * oscillation_frequency_radians)
			* sanitized_settings.oscillation_yaw_amplitude_degrees;
		current_orbit_yaw += yaw_offset;
		break;
	}
	}

	const float time_with_phase = state.pan_elapsed_seconds + state.sway_phase_offset;
	const float base_frequency_radians = (std::max(0.1f, sanitized_settings.sway_frequency) * k_two_pi) * style_time_scale;
	const float orbit_yaw_radians = current_orbit_yaw * k_pi / 180.0f;
	float horizontal_sway = 0.0f;
	float vertical_sway = 0.0f;
	if (sanitized_settings.active_style != CinematicStyle::SlowOrbit)
	{
		horizontal_sway = std::sin(time_with_phase * base_frequency_radians)
			* sanitized_settings.sway_horizontal_amplitude;
		vertical_sway = std::sin((time_with_phase * base_frequency_radians * 0.57f) + 1.2f)
			* sanitized_settings.sway_vertical_amplitude;
	}

	const core::Vec3 orbit_direction(
		std::cos(orbit_yaw_radians),
		std::sin(orbit_yaw_radians),
		0.0f);
	const core::Vec3 orbit_right(-orbit_direction.y, orbit_direction.x, 0.0f);

	pose.location = {
		focus.focus_point.x + (orbit_direction.x * state.orbit_radius) + (orbit_right.x * horizontal_sway),
		focus.focus_point.y + (orbit_direction.y * state.orbit_radius) + (orbit_right.y * horizontal_sway),
		focus.focus_point.z + state.camera_height_offset + vertical_sway};
	pose.rotation = look_at_rotation(pose.location, focus.focus_point);
	return pose;
}

FocusTarget make_focus_from_bounds(const Bounds3& bounds, const float padding_scale)
{
	FocusTarget focus;
	focus.focus_point = bounds.center();
	const float padded_radius = std::max(60.0f, bounds.radius_xy() * padding_scale);
	focus.orbit_radius_hint = padded_radius;
	focus.height_offset = core::math::clamp(
		bounds.max.z - bounds.min.z + 80.0f,
		30.0f,
		160.0f);
	return focus;
}

void apply_orbit_radius_zoom(
	CinematicRuntimeState& state,
	const ZoomInput& input,
	const float min_radius,
	const float max_radius,
	const float wheel_step)
{
	float delta = 0.0f;
	if (std::fabs(input.discrete_wheel_delta) > 1e-4f)
	{
		delta = -input.discrete_wheel_delta * wheel_step;
	}
	else if (std::fabs(input.analog_wheel_axis) > 1e-4f)
	{
		delta = -input.analog_wheel_axis * wheel_step;
	}

	if (std::fabs(delta) <= 1e-4f)
	{
		return;
	}

	state.orbit_radius = core::math::clamp(
		state.orbit_radius + delta,
		min_radius,
		max_radius);
}

} // namespace metaagent::camera
