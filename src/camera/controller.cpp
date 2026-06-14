#include "camera/controller.hpp"

namespace metaagent::camera {

void CameraController::tick_zoom(const ZoomInput& input)
{
	apply_zoom_input(zoom_settings_, input);
}

void CameraController::enable_cinematic(
	const core::Vec3& current_camera_location,
	const FocusTarget& focus)
{
	focus_target_ = focus;
	initialize_cinematic_state_from_pose(cinematic_state_, cinematic_settings_, current_camera_location, focus);
}

void CameraController::disable_cinematic()
{
	cinematic_state_.mode_enabled = false;
	cinematic_state_.pan_elapsed_seconds = 0.0f;
}

CameraPose CameraController::tick_cinematic(const FocusTarget& focus, const float delta_time_seconds)
{
	focus_target_ = focus;
	return compute_cinematic_pose(cinematic_settings_, cinematic_state_, focus_target_, delta_time_seconds);
}

CameraController& default_controller()
{
	static CameraController controller;
	return controller;
}

} // namespace metaagent::camera
