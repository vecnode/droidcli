#pragma once

#include "export.hpp"
#include "particle/forming_types.hpp"
#include "particle/pattern_types.hpp"
#include "particle/return_types.hpp"

namespace metaagent::particle {

enum class RepresentationMacroPhase : uint8_t {
	Idle = 0,
	Prepare,
	Express,
	Sustain,
	Release
};

enum class TransitionTrigger : uint8_t {
	Start = 0,
	Advance,
	Retreat,
	Timeout,
	Cancel,
	Morph,
	Dissipate,
	Ready
};

enum class TransitionAction : uint8_t {
	None = 0,
	EnterState,
	CompleteRun,
	BeginConfiguredReturn,
	BeginPatternStart,
	RequestDissipate
};

struct RepresentationPhase {
	float normalized_time = 0.0f;
	float blend_alpha = 0.0f;
	float authority_weight = 0.0f;
	float visibility = 1.0f;
	float emphasis = 1.0f;
};

struct RepresentationFrame {
	RepresentationMacroPhase macro_phase = RepresentationMacroPhase::Idle;
	PatternState pattern_state = PatternState::Idle;
	RepresentationPhase phase;
	core::Vec3 pattern_center;
	bool pattern_active = false;
	bool anticipating_motion = false;
	bool dissipating_motion = false;
	bool use_return_hold_blend = false;
	float anticipation_elapsed_seconds = 0.0f;
	float anticipation_amplitude_cm = 12.0f;
	float anticipation_frequency_hz = 1.2f;
	float anticipation_idle_blend_duration_seconds = 0.35f;
	float anticipation_handoff_elapsed_seconds = -1.0f;
	float forming_anticipation_carryover_duration_seconds = 0.35f;
	float forming_state_elapsed_seconds = 0.0f;
	float forming_duration_seconds = 1.5f;
	float forming_delta_time_seconds = 0.0f;
	float forming_steering_weight = 0.0f;
	float dissipate_visibility = 1.0f;
	FormingSettings forming_settings;
	bool return_uses_motion_solver = false;
	FormingSettings return_motion_settings;
	core::Array<core::Vec3> baseline_world_positions;
	core::Array<core::Vec3> pattern_world_targets;
	core::Array<core::Vec3> idle_baseline_world_positions;
	core::Array<core::Vec3> return_hold_positions;
	core::Array<core::Vec3> return_rest_positions;
	core::Array<core::Vec3> dissipate_start_positions;
	core::Array<core::Vec3> forming_steering_offsets;
	core::Array<core::Vec3> state_effect_offsets;
};

struct TransitionContext {
	PatternState state = PatternState::Idle;
	bool awaiting_async_mask = false;
	int32_t pattern_target_count = 0;
	bool manual_state_advance = true;
	bool skip_return_on_cancel = false;
	bool dissipate_return_mode = false;
	float state_elapsed_seconds = 0.0f;
	float form_duration_seconds = 1.5f;
	float hold_duration_seconds = 0.5f;
	float return_duration_seconds = 1.5f;
	float dissipate_duration_seconds = 1.2f;
};

struct TransitionResult {
	bool handled = false;
	TransitionAction action = TransitionAction::None;
	PatternState new_state = PatternState::Idle;
	bool restore_idle_baseline_on_enter = false;
	bool clear_pattern_start_log = false;
};

class RepresentationMapping {
public:
	METAAGENT_API static RepresentationMacroPhase macro_phase_from_pattern_state(PatternState state);
	METAAGENT_API static core::String get_macro_phase_display_name(RepresentationMacroPhase macro_phase);
	METAAGENT_API static core::String pattern_state_to_string(PatternState state);
};

} // namespace metaagent::particle
