#pragma once

#include "export.hpp"

namespace metaagent::input {

struct InputPolicy {
	bool block_move = true;
	bool block_look = true;
	bool block_mouse_buttons = true;
	bool allow_mouse_wheel_zoom = false;
	bool allow_gui_clicks = false;
};

METAAGENT_API InputPolicy policy_for_runtime(
	bool gui_panel_visible,
	bool character_input_enabled,
	bool cinematic_active);

} // namespace metaagent::input
