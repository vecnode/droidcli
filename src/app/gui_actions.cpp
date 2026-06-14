#include "app/gui_actions.hpp"

#include "particle/effect_catalog.hpp"

namespace metaagent::app {
namespace {

CommandId map_camera_gui_action(const core::String& action_id)
{
	if (action_id == "ToggleCinematicCamera")
	{
		return CommandId::ToggleCinematicCamera;
	}
	if (action_id == "FocusParticleCamera")
	{
		return CommandId::ToggleFocusParticles;
	}
	if (action_id == "CycleCinematicStyle")
	{
		return CommandId::CycleCinematicStyle;
	}
	return CommandId::Unknown;
}

CommandId map_networking_gui_action(const core::String& action_id)
{
	if (action_id == "StartAudio")
	{
		return CommandId::StartPlatformAudio;
	}
	if (action_id == "StartImage")
	{
		return CommandId::StartPlatformImage;
	}
	return CommandId::Unknown;
}

CommandId map_particle_gui_action(const core::String& action_id)
{
	const particle::ParticleGuiActionSpec* spec = particle::find_particle_gui_action(action_id);
	if (!spec)
	{
		return CommandId::Unknown;
	}

	if (spec->dispatch_kind == particle::ParticleGuiDispatchKind::LoadPreviewPng)
	{
		return CommandId::LoadPreviewImage;
	}

	if (spec->dispatch_kind == particle::ParticleGuiDispatchKind::ToggleStateEffect)
	{
		if (spec->effect_id == particle::state_effect_ids::Cohesion)
		{
			return CommandId::ToggleStateEffectCohesion;
		}
		if (spec->effect_id == particle::state_effect_ids::Turbulence)
		{
			return CommandId::ToggleStateEffectTurbulence;
		}
		return CommandId::Unknown;
	}

	if (spec->effect_id == "PatternStepBackward")
	{
		return CommandId::PatternStepBackward;
	}

	if (spec->effect_id == "PatternStepForward")
	{
		return CommandId::PatternStepForward;
	}

	return CommandId::ParticleGuiEffect;
}

CommandId map_service_gui_action(const core::String& action_id)
{
	if (action_id == "ToggleAutopilot")
	{
		return CommandId::ToggleAutopilot;
	}
	if (action_id == "ToggleRecording")
	{
		return CommandId::ToggleRecording;
	}
	if (action_id == "ReportRecording")
	{
		return CommandId::ReportRecordingStatus;
	}
	return CommandId::Unknown;
}

} // namespace

CommandId command_for_gui_action(const core::String& action_id)
{
	if (action_id == "ToggleHelpPanel")
	{
		return CommandId::ToggleGuiHelp;
	}
	if (action_id == "QuitApplication")
	{
		return CommandId::QuitApplication;
	}

	const CommandId networking_command = map_networking_gui_action(action_id);
	if (networking_command != CommandId::Unknown)
	{
		return networking_command;
	}

	const CommandId particle_command = map_particle_gui_action(action_id);
	if (particle_command != CommandId::Unknown)
	{
		return particle_command;
	}

	const CommandId service_command = map_service_gui_action(action_id);
	if (service_command != CommandId::Unknown)
	{
		return service_command;
	}

	return map_camera_gui_action(action_id);
}

CommandResult validate_gui_action(const core::String& action_id, const session::RuntimeSession& session)
{
	const CommandId command = command_for_gui_action(action_id);
	if (command != CommandId::Unknown)
	{
		return validate_command(command, session);
	}

	CommandResult result;
	result.handled = true;
	result.success = session.active;
	if (!result.success)
	{
		result.user_message = "MetaAgent runtime is inactive.";
	}
	return result;
}

} // namespace metaagent::app
