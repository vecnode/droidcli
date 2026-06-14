#include "particle/forming_types.hpp"

namespace metaagent::particle {

FormingMode FormingSettings::sanitize_mode(const FormingMode mode)
{
	switch (mode)
	{
	case FormingMode::DirectLerp:
	case FormingMode::ArcLift:
	case FormingMode::SpiralIn:
	case FormingMode::StaggeredWave:
	case FormingMode::SpringChase:
		return mode;
	default:
		return FormingMode::DirectLerp;
	}
}

core::String FormingSettings::get_mode_display_name() const
{
	switch (sanitize_mode(mode))
	{
	case FormingMode::DirectLerp: return "Direct Lerp";
	case FormingMode::ArcLift: return "Arc Lift";
	case FormingMode::SpiralIn: return "Spiral In";
	case FormingMode::StaggeredWave: return "Staggered Wave";
	case FormingMode::SpringChase: return "Spring Chase";
	default: return "Direct Lerp";
	}
}

void FormingSettings::cycle_mode()
{
	switch (sanitize_mode(mode))
	{
	case FormingMode::DirectLerp: mode = FormingMode::ArcLift; break;
	case FormingMode::ArcLift: mode = FormingMode::SpiralIn; break;
	case FormingMode::SpiralIn: mode = FormingMode::StaggeredWave; break;
	case FormingMode::StaggeredWave: mode = FormingMode::SpringChase; break;
	case FormingMode::SpringChase:
	default: mode = FormingMode::DirectLerp; break;
	}
}

} // namespace metaagent::particle
