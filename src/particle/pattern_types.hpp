#pragma once

#include "core/types.hpp"
#include "export.hpp"
#include "particle/forming_types.hpp"
#include "particle/return_types.hpp"
#include "particle/shape_types.hpp"

namespace metaagent::particle {

enum class PatternState : uint8_t {
	Idle = 0,
	Preparing,
	Anticipating,
	Forming,
	Holding,
	Returning,
	Dissipating,
	Releasing
};

enum class PatternPreset : uint8_t {
	Normal = 0,
	Slow,
	Dramatic,
	Snappy,
	Dreamy,
	Custom
};

enum class ActuationMode : uint8_t {
	Direct = 0,
	Parameters,
	Hybrid
};

struct PatternConfig {
	float form_duration_seconds = 1.5f;
	float hold_duration_seconds = 0.5f;
	float return_duration_seconds = 1.5f;
	float dissipate_duration_seconds = 1.2f;
	float anticipation_amplitude_cm = 12.0f;
	float anticipation_frequency_hz = 1.2f;
	float anticipation_idle_blend_duration_seconds = 0.35f;
	float forming_anticipation_carryover_duration_seconds = 0.35f;
	float grid_spacing_cm = 12.0f;
	ShapeDefinition shape;
	FormingSettings forming;
	ReturnSettings return_settings;
	float hold_pulse_amplitude = 0.04f;
	float hold_pulse_frequency_hz = 0.75f;
	PatternPreset active_preset = PatternPreset::Normal;

	METAAGENT_API static PatternConfig make_from_preset(PatternPreset preset);
	METAAGENT_API void apply_preset(PatternPreset preset);
	METAAGENT_API void cycle_preset();
	METAAGENT_API core::String get_preset_display_name() const;
};

struct PatternRuntime {
	PatternState state = PatternState::Idle;
	float phase = 0.0f;
	float state_elapsed_seconds = 0.0f;
	int32_t pattern_columns = 0;
	core::Vec3 pattern_center;
	ShapeType active_shape = ShapeType::SquareGrid;
	ShapeFrame active_shape_frame;
	core::String shape_debug_info;
	PatternConfig active_config;
	core::Array<core::Vec3> baseline_world_positions;
	core::Array<core::Vec3> idle_baseline_world_positions;
	core::Array<core::Vec3> pattern_world_targets;
	/** Mask targets from the last successful shape build; preserved across manual hold freezes. */
	core::Array<core::Vec3> canonical_pattern_world_targets;
	core::Array<core::Vec3> return_hold_positions;
	core::Array<core::Vec3> return_rest_positions;
	core::Array<core::Vec3> trajectory_world_positions;
	core::Array<core::Vec3> dissipate_start_positions;
	bool awaiting_async_mask = false;
	float anticipation_handoff_elapsed_seconds = -1.0f;
	core::Array<float> form_curve_samples;
	core::Array<float> asset_return_curve_samples;
	core::Array<core::String> active_pattern_tags;
};

} // namespace metaagent::particle
