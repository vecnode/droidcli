#pragma once

#include "export.hpp"
#include "camera/rig.hpp"

namespace metaagent::camera {

class CameraController {
public:
	METAAGENT_API void tick_zoom(const ZoomInput& input);

	METAAGENT_API void enable_cinematic(
		const core::Vec3& current_camera_location,
		const FocusTarget& focus);

	METAAGENT_API void disable_cinematic();

	METAAGENT_API CameraPose tick_cinematic(const FocusTarget& focus, float delta_time_seconds);

	METAAGENT_API void set_focus(const FocusTarget& focus) { focus_target_ = focus; }

	METAAGENT_API const FocusTarget& get_focus() const { return focus_target_; }

	METAAGENT_API ZoomSettings& zoom_settings() { return zoom_settings_; }

	METAAGENT_API CinematicSettings& cinematic_settings() { return cinematic_settings_; }

	METAAGENT_API CinematicRuntimeState& cinematic_state() { return cinematic_state_; }

	METAAGENT_API const ZoomSettings& zoom_settings() const { return zoom_settings_; }

	METAAGENT_API bool is_cinematic_enabled() const { return cinematic_state_.mode_enabled; }

private:
	ZoomSettings zoom_settings_;
	CinematicSettings cinematic_settings_;
	CinematicRuntimeState cinematic_state_;
	FocusTarget focus_target_;
};

METAAGENT_API CameraController& default_controller();

} // namespace metaagent::camera
