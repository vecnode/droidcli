#pragma once

#include "app/commands.hpp"
#include "export.hpp"
#include "session/types.hpp"

namespace metaagent::app {

METAAGENT_API CommandId command_for_gui_action(const core::String& action_id);

METAAGENT_API CommandResult validate_gui_action(
	const core::String& action_id,
	const session::RuntimeSession& session);

} // namespace metaagent::app
