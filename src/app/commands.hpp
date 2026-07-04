#pragma once

#include "export.hpp"
#include "session/types.hpp"

namespace metaagent::app {

enum class CommandId {
	Unknown = 0,
	ToggleNetworkingRuntime,
	ToggleGuiHelp,
	StartPlatformAudio,
	StartPlatformImage,
	ToggleAutopilot,
	ToggleRecording,
	ReportRecordingStatus,
	QuitApplication,
};

struct CommandResult {
	bool handled = false;
	bool success = false;
	core::String user_message;
};

METAAGENT_API CommandId parse_command_name(const core::String& name);

METAAGENT_API core::String command_display_name(CommandId command);

METAAGENT_API CommandResult validate_command(CommandId command, const session::RuntimeSession& session);

} // namespace metaagent::app
