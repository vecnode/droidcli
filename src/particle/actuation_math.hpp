#pragma once

#include "export.hpp"
#include "particle/forming_solver.hpp"
#include "particle/pattern_types.hpp"

namespace metaagent::particle {

struct ActuationComposeInput {
	PatternState pattern_state = PatternState::Idle;
	float blend_alpha = 0.0f;
	core::Vec3 pattern_center;
	bool use_return_hold_blend = false;
	bool anticipating_motion = false;
	bool dissipating_motion = false;
	float anticipation_elapsed_seconds = 0.0f;
	float anticipation_amplitude_cm = 12.0f;
	float anticipation_frequency_hz = 1.2f;
	float anticipation_idle_blend_duration_seconds = 0.35f;
	float anticipation_handoff_elapsed_seconds = -1.0f;
	float forming_anticipation_carryover_duration_seconds = 0.35f;
	float forming_state_elapsed_seconds = 0.0f;
	float forming_duration_seconds = 1.0f;
	float forming_delta_time_seconds = 0.0f;
	float forming_steering_weight = 0.0f;
	const FormingSettings* forming_settings = nullptr;
	const core::Array<core::Vec3>* baseline_world_positions = nullptr;
	const core::Array<core::Vec3>* pattern_world_targets = nullptr;
	const core::Array<core::Vec3>* return_hold_positions = nullptr;
	const core::Array<core::Vec3>* return_rest_positions = nullptr;
	const core::Array<core::Vec3>* dissipate_start_positions = nullptr;
	const core::Array<core::Vec3>* idle_baseline_world_positions = nullptr;
	const core::Array<core::Vec3>* forming_steering_offsets = nullptr;
};

struct ActuationComposeResult {
	bool valid = false;
	core::Vec3 world_position;
};

class ActuationMath {
public:
	METAAGENT_API static float evaluate_phase_for_state(
		PatternState state,
		float normalized_time_in_state,
		const PatternConfig& active_config,
		const core::Array<float>& form_curve_samples,
		const core::Array<float>& asset_return_curve_samples);

	METAAGENT_API static core::Vec3 compute_anticipation_world_position(
		const core::Vec3& idle_baseline,
		int32_t global_index,
		const core::Vec3& pattern_center,
		float anticipation_elapsed_seconds,
		float anticipation_amplitude_cm,
		float anticipation_frequency_hz,
		float anticipation_idle_blend_duration_seconds = 0.35f);

	METAAGENT_API static void build_anticipation_world_positions(
		const core::Array<core::Vec3>& idle_baseline_world_positions,
		const core::Vec3& pattern_center,
		float anticipation_elapsed_seconds,
		float anticipation_amplitude_cm,
		float anticipation_frequency_hz,
		core::Array<core::Vec3>& out_world_positions,
		float anticipation_idle_blend_duration_seconds = 0.35f);

	METAAGENT_API static float compute_actuation_blend_alpha(PatternState state, float phase);

	METAAGENT_API static ActuationComposeResult compose_particle_world_position(
		const ActuationComposeInput& input,
		int32_t global_index);

	METAAGENT_API static void compose_world_positions(
		const ActuationComposeInput& input,
		core::Array<core::Vec3>& out_world_positions);
};

} // namespace metaagent::particle
