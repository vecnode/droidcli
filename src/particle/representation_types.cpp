#include "particle/representation_types.hpp"

namespace metaagent::particle {

RepresentationMacroPhase RepresentationMapping::macro_phase_from_pattern_state(const PatternState state)
{
	switch (state)
	{
	case PatternState::Preparing:
		return RepresentationMacroPhase::Idle;
	case PatternState::Anticipating:
		return RepresentationMacroPhase::Prepare;
	case PatternState::Forming:
		return RepresentationMacroPhase::Express;
	case PatternState::Holding:
		return RepresentationMacroPhase::Sustain;
	case PatternState::Returning:
	case PatternState::Dissipating:
		return RepresentationMacroPhase::Release;
	case PatternState::Idle:
	default:
		return RepresentationMacroPhase::Idle;
	}
}

core::String RepresentationMapping::get_macro_phase_display_name(const RepresentationMacroPhase macro_phase)
{
	switch (macro_phase)
	{
	case RepresentationMacroPhase::Prepare: return "Prepare";
	case RepresentationMacroPhase::Express: return "Express";
	case RepresentationMacroPhase::Sustain: return "Sustain";
	case RepresentationMacroPhase::Release: return "Release";
	case RepresentationMacroPhase::Idle:
	default: return "Idle";
	}
}

core::String RepresentationMapping::pattern_state_to_string(const PatternState state)
{
	switch (state)
	{
	case PatternState::Preparing: return "Preparing";
	case PatternState::Anticipating: return "Anticipating";
	case PatternState::Forming: return "Forming";
	case PatternState::Holding: return "Holding";
	case PatternState::Returning: return "Returning";
	case PatternState::Dissipating: return "Dissipating";
	case PatternState::Releasing: return "Releasing";
	case PatternState::Idle:
	default: return "Idle";
	}
}

} // namespace metaagent::particle
