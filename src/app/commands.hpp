#pragma once

#include "export.hpp"
#include "session/types.hpp"

namespace droidcli::app {

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

DROIDCLI_API CommandId parse_command_name(const core::String& name);

DROIDCLI_API core::String command_display_name(CommandId command);

DROIDCLI_API CommandResult validate_command(CommandId command, const session::RuntimeSession& session);

} // namespace droidcli::app
