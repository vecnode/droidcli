#include "app/commands.hpp"

#include <algorithm>
#include <cctype>

namespace metaagent::app {
namespace {

core::String to_lower_ascii(core::String value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool feature_enabled(const session::RuntimeSession& session, const char* feature_name)
{
	if (std::string(feature_name) == "input")
	{
		return session.features.input;
	}
	if (std::string(feature_name) == "camera")
	{
		return session.features.camera;
	}
	if (std::string(feature_name) == "networking")
	{
		return session.features.networking;
	}
	if (std::string(feature_name) == "ui")
	{
		return session.features.ui;
	}
	if (std::string(feature_name) == "particle")
	{
		return session.features.particle;
	}
	if (std::string(feature_name) == "ai")
	{
		return session.features.ai;
	}
	if (std::string(feature_name) == "recording")
	{
		return session.features.recording;
	}
	return session.active;
}

} // namespace

CommandId parse_command_name(const core::String& name)
{
	const core::String normalized = to_lower_ascii(name);
	if (normalized == "pattern_step_forward" || normalized == "pattern>>" || normalized == "step_forward")
	{
		return CommandId::PatternStepForward;
	}
	if (normalized == "pattern_step_backward" || normalized == "pattern<<" || normalized == "step_backward")
	{
		return CommandId::PatternStepBackward;
	}
	if (normalized == "toggle_cinematic_camera" || normalized == "cinematic")
	{
		return CommandId::ToggleCinematicCamera;
	}
	if (normalized == "toggle_focus_particles" || normalized == "focus_particles")
	{
		return CommandId::ToggleFocusParticles;
	}
	if (normalized == "cycle_cinematic_style" || normalized == "cinematic_style")
	{
		return CommandId::CycleCinematicStyle;
	}
	if (normalized == "toggle_networking_runtime" || normalized == "networking")
	{
		return CommandId::ToggleNetworkingRuntime;
	}
	if (normalized == "toggle_gui_help" || normalized == "gui_help")
	{
		return CommandId::ToggleGuiHelp;
	}
	if (normalized == "load_preview_image" || normalized == "preview_image")
	{
		return CommandId::LoadPreviewImage;
	}
	if (normalized == "start_platform_audio" || normalized == "start audio")
	{
		return CommandId::StartPlatformAudio;
	}
	if (normalized == "start_platform_image" || normalized == "start image")
	{
		return CommandId::StartPlatformImage;
	}
	if (normalized == "toggle_state_effect_cohesion" || normalized == "state_effect_cohesion")
	{
		return CommandId::ToggleStateEffectCohesion;
	}
	if (normalized == "toggle_state_effect_turbulence" || normalized == "state_effect_turbulence")
	{
		return CommandId::ToggleStateEffectTurbulence;
	}
	if (normalized == "particle_gui_effect" || normalized == "cycle_preset" || normalized == "cycle_sampling"
		|| normalized == "cycle_forming" || normalized == "cycle_returning" || normalized == "cycle_overlay")
	{
		return CommandId::ParticleGuiEffect;
	}
	if (normalized == "quit_application" || normalized == "quit")
	{
		return CommandId::QuitApplication;
	}
	if (normalized == "toggle_autopilot" || normalized == "autopilot")
	{
		return CommandId::ToggleAutopilot;
	}
	if (normalized == "toggle_recording" || normalized == "recording")
	{
		return CommandId::ToggleRecording;
	}
	if (normalized == "report_recording_status" || normalized == "recording_status")
	{
		return CommandId::ReportRecordingStatus;
	}
	return CommandId::Unknown;
}

core::String command_display_name(const CommandId command)
{
	switch (command)
	{
	case CommandId::PatternStepForward:
		return "Pattern Step Forward";
	case CommandId::PatternStepBackward:
		return "Pattern Step Backward";
	case CommandId::ToggleCinematicCamera:
		return "Toggle Cinematic Camera";
	case CommandId::ToggleFocusParticles:
		return "Toggle Focus Particles";
	case CommandId::CycleCinematicStyle:
		return "Cycle Cinematic Style";
	case CommandId::ToggleNetworkingRuntime:
		return "Toggle Networking Runtime";
	case CommandId::ToggleGuiHelp:
		return "Toggle GUI Help";
	case CommandId::LoadPreviewImage:
		return "Load Preview Image";
	case CommandId::StartPlatformAudio:
		return "Start Platform Audio";
	case CommandId::StartPlatformImage:
		return "Start Platform Image";
	case CommandId::ToggleStateEffectCohesion:
		return "Toggle Cohesion Overlay";
	case CommandId::ToggleStateEffectTurbulence:
		return "Toggle Turbulence Overlay";
	case CommandId::ParticleGuiEffect:
		return "Particle Effect";
	case CommandId::ToggleAutopilot:
		return "Toggle Autopilot";
	case CommandId::ToggleRecording:
		return "Toggle Recording";
	case CommandId::ReportRecordingStatus:
		return "Report Recording Status";
	case CommandId::QuitApplication:
		return "Quit Application";
	default:
		return "Unknown Command";
	}
}

CommandResult validate_command(const CommandId command, const session::RuntimeSession& session)
{
	CommandResult result;
	result.handled = command != CommandId::Unknown;
	if (!result.handled)
	{
		result.user_message = "Unknown command.";
		return result;
	}

	if (!session.active)
	{
		result.success = false;
		result.user_message = "MetaAgent runtime is inactive.";
		return result;
	}

	switch (command)
	{
	case CommandId::PatternStepForward:
	case CommandId::PatternStepBackward:
	case CommandId::LoadPreviewImage:
	case CommandId::ToggleStateEffectCohesion:
	case CommandId::ToggleStateEffectTurbulence:
	case CommandId::ParticleGuiEffect:
		result.success = feature_enabled(session, "particle");
		if (!result.success)
		{
			result.user_message = "Particle runtime is disabled.";
		}
		return result;
	case CommandId::ToggleCinematicCamera:
	case CommandId::ToggleFocusParticles:
	case CommandId::CycleCinematicStyle:
		result.success = feature_enabled(session, "camera");
		if (!result.success)
		{
			result.user_message = "Camera runtime is disabled.";
		}
		return result;
	case CommandId::ToggleNetworkingRuntime:
	case CommandId::StartPlatformAudio:
	case CommandId::StartPlatformImage:
		result.success = feature_enabled(session, "networking");
		if (!result.success)
		{
			result.user_message = "Networking runtime is disabled.";
		}
		return result;
	case CommandId::ToggleGuiHelp:
	case CommandId::QuitApplication:
		result.success = feature_enabled(session, "ui");
		if (!result.success)
		{
			result.user_message = "UI runtime is disabled.";
		}
		return result;
	case CommandId::ToggleAutopilot:
		result.success = feature_enabled(session, "ai");
		if (!result.success)
		{
			result.user_message = "AI runtime is disabled.";
		}
		return result;
	case CommandId::ToggleRecording:
	case CommandId::ReportRecordingStatus:
		result.success = feature_enabled(session, "recording");
		if (!result.success)
		{
			result.user_message = "Recording runtime is disabled.";
		}
		return result;
	default:
		result.success = false;
		result.user_message = "Command is not available.";
		return result;
	}
}

} // namespace metaagent::app
