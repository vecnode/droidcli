#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::app;
	using namespace metaagent::session;

	RuntimeSession active_session;
	active_session.active = true;
	active_session.features.ui = true;
	active_session.features.networking = true;
	active_session.features.recording = true;
	active_session.features.ai = true;

	assert(validate_command(CommandId::StartPlatformAudio, active_session).success);
	assert(validate_command(CommandId::StartPlatformImage, active_session).success);
	assert(validate_command(CommandId::ToggleRecording, active_session).success);
	assert(validate_command(CommandId::ToggleAutopilot, active_session).success);
	assert(parse_command_name("start audio") == CommandId::StartPlatformAudio);
	assert(parse_command_name("toggle_recording") == CommandId::ToggleRecording);
	assert(parse_command_name("autopilot") == CommandId::ToggleAutopilot);
	assert(parse_command_name("no_such_command") == CommandId::Unknown);
	assert(!validate_command(CommandId::Unknown, active_session).handled);

	RuntimeSession networking_off = active_session;
	networking_off.features.networking = false;
	assert(!validate_command(CommandId::StartPlatformAudio, networking_off).success);

	RuntimeSession recording_off = active_session;
	recording_off.features.recording = false;
	assert(!validate_command(CommandId::ToggleRecording, recording_off).success);

	RuntimeSession inactive = active_session;
	inactive.active = false;
	assert(!validate_command(CommandId::QuitApplication, inactive).success);

	return 0;
}
