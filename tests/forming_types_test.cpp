#include "particle/forming_types.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using metaagent::particle::FormingMode;
	using metaagent::particle::FormingSettings;

	FormingSettings settings;
	assert(settings.mode == FormingMode::DirectLerp);

	settings.mode = FormingMode::DirectLerp;
	settings.cycle_mode();
	assert(settings.mode == FormingMode::ArcLift);
	assert(settings.get_mode_display_name() == "Arc Lift");

	settings.cycle_mode();
	settings.cycle_mode();
	settings.cycle_mode();
	settings.cycle_mode();
	assert(settings.mode == FormingMode::DirectLerp);

	assert(FormingSettings::sanitize_mode(static_cast<FormingMode>(99)) == FormingMode::DirectLerp);

	std::cout << "forming_types_test passed\n";
	return 0;
}
