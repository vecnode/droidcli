#include "particle/return_types.hpp"

namespace metaagent::particle {

ReturnMode ReturnSettings::sanitize_mode(const ReturnMode mode)
{
	switch (mode)
	{
	case ReturnMode::DirectLerp:
	case ReturnMode::ArcLift:
	case ReturnMode::SpiralIn:
	case ReturnMode::DissipateToCenter:
		return mode;
	default:
		return ReturnMode::DirectLerp;
	}
}

bool ReturnSettings::uses_motion_solver() const
{
	switch (sanitize_mode(mode))
	{
	case ReturnMode::ArcLift:
	case ReturnMode::SpiralIn:
		return true;
	default:
		return false;
	}
}

FormingSettings ReturnSettings::as_forming_settings() const
{
	FormingSettings out;
	out.arc_lift_height_cm = arc_lift_height_cm;
	out.spiral_turns = spiral_turns;

	switch (sanitize_mode(mode))
	{
	case ReturnMode::ArcLift: out.mode = FormingMode::ArcLift; break;
	case ReturnMode::SpiralIn: out.mode = FormingMode::SpiralIn; break;
	case ReturnMode::DirectLerp:
	case ReturnMode::DissipateToCenter:
	default: out.mode = FormingMode::DirectLerp; break;
	}

	return out;
}

core::String ReturnSettings::get_mode_display_name() const
{
	switch (sanitize_mode(mode))
	{
	case ReturnMode::DirectLerp: return "Direct Lerp";
	case ReturnMode::ArcLift: return "Arc Lift";
	case ReturnMode::SpiralIn: return "Spiral In";
	case ReturnMode::DissipateToCenter: return "Dissipate To Center";
	default: return "Direct Lerp";
	}
}

const core::Array<float>* ReturnSettings::get_return_curve_for_mode() const
{
	switch (sanitize_mode(mode))
	{
	case ReturnMode::ArcLift:
		return arc_lift_curve_samples.empty() ? nullptr : &arc_lift_curve_samples;
	case ReturnMode::SpiralIn:
		return spiral_in_curve_samples.empty() ? nullptr : &spiral_in_curve_samples;
	case ReturnMode::DirectLerp:
	default:
		return direct_lerp_curve_samples.empty() ? nullptr : &direct_lerp_curve_samples;
	}
}

void ReturnSettings::cycle_mode()
{
	switch (sanitize_mode(mode))
	{
	case ReturnMode::DirectLerp: mode = ReturnMode::ArcLift; break;
	case ReturnMode::ArcLift: mode = ReturnMode::SpiralIn; break;
	case ReturnMode::SpiralIn: mode = ReturnMode::DissipateToCenter; break;
	case ReturnMode::DissipateToCenter:
	default: mode = ReturnMode::DirectLerp; break;
	}
}

} // namespace metaagent::particle
