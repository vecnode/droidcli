#include "input/policy.hpp"

namespace metaagent::input {

InputPolicy policy_for_runtime(
	const bool gui_panel_visible,
	const bool character_input_enabled,
	const bool cinematic_active)
{
	InputPolicy policy;
	policy.allow_gui_clicks = gui_panel_visible;
	policy.allow_mouse_wheel_zoom = !gui_panel_visible && cinematic_active;

	if (gui_panel_visible)
	{
		policy.block_move = true;
		policy.block_look = true;
		policy.block_mouse_buttons = false;
		return policy;
	}

	policy.block_move = true;
	policy.block_look = true;
	policy.block_mouse_buttons = true;
	(void)character_input_enabled;
	return policy;
}

} // namespace metaagent::input
