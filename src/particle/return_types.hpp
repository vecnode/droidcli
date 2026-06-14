#pragma once

#include "particle/forming_types.hpp"
#include "export.hpp"

namespace metaagent::particle {

enum class ReturnMode : uint8_t {
	DirectLerp = 0,
	ArcLift,
	SpiralIn,
	DissipateToCenter
};

struct ReturnSettings {
	ReturnMode mode = ReturnMode::DirectLerp;
	float arc_lift_height_cm = 80.0f;
	float spiral_turns = 1.5f;
	core::Array<float> direct_lerp_curve_samples;
	core::Array<float> arc_lift_curve_samples;
	core::Array<float> spiral_in_curve_samples;

	METAAGENT_API core::String get_mode_display_name() const;
	METAAGENT_API const core::Array<float>* get_return_curve_for_mode() const;
	METAAGENT_API void cycle_mode();
	METAAGENT_API bool uses_motion_solver() const;
	METAAGENT_API FormingSettings as_forming_settings() const;
	METAAGENT_API static ReturnMode sanitize_mode(ReturnMode mode);
};

} // namespace metaagent::particle
