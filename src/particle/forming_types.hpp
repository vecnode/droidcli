#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::particle {

enum class FormingMode : uint8_t {
	DirectLerp = 0,
	ArcLift,
	SpiralIn,
	StaggeredWave,
	SpringChase
};

struct FormingSettings {
	FormingMode mode = FormingMode::DirectLerp;
	float arc_lift_height_cm = 80.0f;
	float spiral_turns = 1.5f;
	float wave_spread = 0.45f;
	float spring_overshoot = 0.12f;

	METAAGENT_API core::String get_mode_display_name() const;
	METAAGENT_API void cycle_mode();
	METAAGENT_API static FormingMode sanitize_mode(FormingMode mode);
};

} // namespace metaagent::particle
