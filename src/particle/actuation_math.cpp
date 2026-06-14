#include "particle/actuation_math.hpp"

#include "core/math.hpp"
#include "particle/forming_solver.hpp"

#include <cmath>

namespace metaagent::particle {
namespace {

bool is_valid_index(const core::Array<core::Vec3>* array, const int32_t index)
{
	return array && index >= 0 && index < static_cast<int32_t>(array->size());
}

} // namespace

float ActuationMath::evaluate_phase_for_state(
	const PatternState state,
	const float normalized_time_in_state,
	const PatternConfig& active_config,
	const core::Array<float>& form_curve_samples,
	const core::Array<float>& asset_return_curve_samples)
{
	const float clamped_time = core::math::clamp(normalized_time_in_state, 0.0f, 1.0f);

	if (state == PatternState::Forming && !form_curve_samples.empty())
	{
		return core::math::clamp(
			core::math::evaluate_curve01(
				clamped_time,
				form_curve_samples.data(),
				static_cast<int>(form_curve_samples.size())),
			0.0f,
			1.0f);
	}

	if (state == PatternState::Returning)
	{
		const core::Array<float>* mode_return_curve_samples =
			active_config.return_settings.get_return_curve_for_mode();
		const core::Array<float>* effective_return_curve_samples = mode_return_curve_samples;
		if (!effective_return_curve_samples || effective_return_curve_samples->empty())
		{
			effective_return_curve_samples = asset_return_curve_samples.empty()
				? nullptr
				: &asset_return_curve_samples;
		}

		if (effective_return_curve_samples && !effective_return_curve_samples->empty())
		{
			return core::math::clamp(
				1.0f - core::math::evaluate_curve01(
					clamped_time,
					effective_return_curve_samples->data(),
					static_cast<int>(effective_return_curve_samples->size())),
				0.0f,
				1.0f);
		}

		return 1.0f - core::math::smooth_step01(clamped_time);
	}

	return core::math::smooth_step01(clamped_time);
}

core::Vec3 ActuationMath::compute_anticipation_world_position(
	const core::Vec3& idle_baseline,
	const int32_t global_index,
	const core::Vec3& pattern_center,
	const float anticipation_elapsed_seconds,
	const float anticipation_amplitude_cm,
	const float anticipation_frequency_hz,
	const float anticipation_idle_blend_duration_seconds)
{
	core::Vec3 to_center = pattern_center - idle_baseline;
	if (to_center.nearly_zero())
	{
		to_center = {0.0f, 0.0f, 1.0f};
	}
	const core::Vec3 radial_dir = to_center.normalized();
	core::Vec3 tangent_dir = {
		radial_dir.y * 0.0f - radial_dir.z * 1.0f,
		radial_dir.z * 0.0f - radial_dir.x * 0.0f,
		radial_dir.x * 1.0f - radial_dir.y * 0.0f};
	if (tangent_dir.nearly_zero())
	{
		tangent_dir = {1.0f, 0.0f, 0.0f};
	}
	else
	{
		tangent_dir = tangent_dir.normalized();
	}

	float idle_blend_weight = 1.0f;
	if (anticipation_idle_blend_duration_seconds > 1e-4f)
	{
		const float idle_blend_normalized = core::math::clamp(
			anticipation_elapsed_seconds / anticipation_idle_blend_duration_seconds,
			0.0f,
			1.0f);
		idle_blend_weight = core::math::smooth_step01(idle_blend_normalized);
	}

	const float anticipation_radians = anticipation_elapsed_seconds
		* core::math::k_two_pi * std::max(0.1f, anticipation_frequency_hz);
	const float index_phase = static_cast<float>(global_index) * 0.37f;
	const float amplitude = std::max(0.0f, anticipation_amplitude_cm) * idle_blend_weight;
	const float radial_pulse = std::sin(anticipation_radians + index_phase) * amplitude * 0.55f;
	const float orbit_pulse = std::cos(anticipation_radians * 0.85f + index_phase * 1.7f) * amplitude * 0.45f;
	const float vertical_pulse = std::sin(anticipation_radians * 1.35f + index_phase * 0.61f) * amplitude * 0.18f;

	return idle_baseline
		+ radial_dir * radial_pulse
		+ tangent_dir * orbit_pulse
		+ core::Vec3 {0.0f, 0.0f, vertical_pulse};
}

void ActuationMath::build_anticipation_world_positions(
	const core::Array<core::Vec3>& idle_baseline_world_positions,
	const core::Vec3& pattern_center,
	const float anticipation_elapsed_seconds,
	const float anticipation_amplitude_cm,
	const float anticipation_frequency_hz,
	core::Array<core::Vec3>& out_world_positions,
	const float anticipation_idle_blend_duration_seconds)
{
	const int32_t particle_count = static_cast<int32_t>(idle_baseline_world_positions.size());
	out_world_positions.resize(particle_count);
	for (int32_t particle_index = 0; particle_index < particle_count; ++particle_index)
	{
		out_world_positions[static_cast<size_t>(particle_index)] = compute_anticipation_world_position(
			idle_baseline_world_positions[static_cast<size_t>(particle_index)],
			particle_index,
			pattern_center,
			anticipation_elapsed_seconds,
			anticipation_amplitude_cm,
			anticipation_frequency_hz,
			anticipation_idle_blend_duration_seconds);
	}
}

float ActuationMath::compute_actuation_blend_alpha(const PatternState state, const float phase)
{
	if (state == PatternState::Holding)
	{
		return 1.0f;
	}
	return core::math::clamp(phase, 0.0f, 1.0f);
}

ActuationComposeResult ActuationMath::compose_particle_world_position(
	const ActuationComposeInput& input,
	const int32_t global_index)
{
	ActuationComposeResult result;
	if (!input.baseline_world_positions || !input.pattern_world_targets)
	{
		return result;
	}

	const core::Array<core::Vec3>& baseline_world_positions = *input.baseline_world_positions;
	const core::Array<core::Vec3>& pattern_world_targets = *input.pattern_world_targets;
	const float blend_alpha = input.blend_alpha;
	const int32_t total_particle_count = static_cast<int32_t>(baseline_world_positions.size());

	const bool return_blend = input.use_return_hold_blend
		&& is_valid_index(input.return_hold_positions, global_index)
		&& is_valid_index(input.return_rest_positions, global_index);

	const bool use_forming_solver = !return_blend
		&& input.pattern_state == PatternState::Forming
		&& input.forming_settings != nullptr;

	const bool anticipating_motion = !return_blend
		&& input.anticipating_motion
		&& input.pattern_state == PatternState::Anticipating;

	const bool dissipating_motion = !return_blend
		&& input.dissipating_motion
		&& input.pattern_state == PatternState::Dissipating
		&& is_valid_index(input.dissipate_start_positions, global_index);

	core::Vec3 desired_world = core::Vec3 {};
	if (is_valid_index(&pattern_world_targets, global_index))
	{
		desired_world = pattern_world_targets[static_cast<size_t>(global_index)];
	}

	if (return_blend)
	{
		const core::Vec3 hold_pos = (*input.return_hold_positions)[static_cast<size_t>(global_index)];
		const core::Vec3 rest_pos = (*input.return_rest_positions)[static_cast<size_t>(global_index)];

		const bool return_forming_solver = input.forming_settings != nullptr
			&& input.pattern_state == PatternState::Returning;

		if (return_forming_solver)
		{
			FormingContext forming_context;
			forming_context.global_index = global_index;
			forming_context.total_particle_count = total_particle_count;
			forming_context.baseline = rest_pos;
			forming_context.target = hold_pos;
			forming_context.pattern_center = input.pattern_center;
			forming_context.blend_alpha = blend_alpha;
			forming_context.state_elapsed_seconds = input.forming_state_elapsed_seconds;
			forming_context.form_duration_seconds = input.forming_duration_seconds;
			forming_context.delta_time_seconds = input.forming_delta_time_seconds;
			forming_context.settings = input.forming_settings;
			desired_world = FormingSolverRegistry::solve_forming_position(forming_context);
		}
		else
		{
			desired_world = core::math::lerp(rest_pos, hold_pos, blend_alpha);
		}
	}
	else if (!is_valid_index(&baseline_world_positions, global_index)
		|| !is_valid_index(&pattern_world_targets, global_index))
	{
		return result;
	}
	else if (anticipating_motion)
	{
		desired_world = compute_anticipation_world_position(
			baseline_world_positions[static_cast<size_t>(global_index)],
			global_index,
			input.pattern_center,
			input.anticipation_elapsed_seconds,
			input.anticipation_amplitude_cm,
			input.anticipation_frequency_hz,
			input.anticipation_idle_blend_duration_seconds);
	}
	else if (dissipating_motion)
	{
		const core::Vec3 start_position =
			(*input.dissipate_start_positions)[static_cast<size_t>(global_index)];
		desired_world = core::math::lerp(
			start_position,
			input.pattern_center,
			core::math::clamp(blend_alpha, 0.0f, 1.0f));
	}
	else if (use_forming_solver)
	{
		FormingContext forming_context;
		forming_context.global_index = global_index;
		forming_context.total_particle_count = total_particle_count;
		forming_context.baseline = baseline_world_positions[static_cast<size_t>(global_index)];
		forming_context.target = pattern_world_targets[static_cast<size_t>(global_index)];
		forming_context.pattern_center = input.pattern_center;
		forming_context.blend_alpha = blend_alpha;
		forming_context.state_elapsed_seconds = input.forming_state_elapsed_seconds;
		forming_context.form_duration_seconds = input.forming_duration_seconds;
		forming_context.delta_time_seconds = input.forming_delta_time_seconds;
		forming_context.settings = input.forming_settings;
		forming_context.forming_steering_weight = input.forming_steering_weight;
		if (is_valid_index(input.forming_steering_offsets, global_index))
		{
			forming_context.forming_steering_offset =
				(*input.forming_steering_offsets)[static_cast<size_t>(global_index)];
		}

		const core::Vec3 forming_world = FormingSolverRegistry::solve_forming_position(forming_context);

		const bool use_anticipation_carryover = input.anticipation_handoff_elapsed_seconds >= 0.0f
			&& is_valid_index(input.idle_baseline_world_positions, global_index)
			&& input.forming_anticipation_carryover_duration_seconds > 1e-4f
			&& input.forming_state_elapsed_seconds < input.forming_anticipation_carryover_duration_seconds;

		if (use_anticipation_carryover)
		{
			const float carryover_normalized = core::math::clamp(
				input.forming_state_elapsed_seconds / input.forming_anticipation_carryover_duration_seconds,
				0.0f,
				1.0f);
			const float forming_weight = core::math::smooth_step01(carryover_normalized);
			const core::Vec3 continuing_anticipation = compute_anticipation_world_position(
				(*input.idle_baseline_world_positions)[static_cast<size_t>(global_index)],
				global_index,
				input.pattern_center,
				input.anticipation_handoff_elapsed_seconds + input.forming_state_elapsed_seconds,
				input.anticipation_amplitude_cm,
				input.anticipation_frequency_hz,
				input.anticipation_idle_blend_duration_seconds);
			desired_world = core::math::lerp(continuing_anticipation, forming_world, forming_weight);
		}
		else
		{
			desired_world = forming_world;
		}
	}
	else
	{
		desired_world = core::math::lerp(
			baseline_world_positions[static_cast<size_t>(global_index)],
			pattern_world_targets[static_cast<size_t>(global_index)],
			blend_alpha);

		if (input.forming_steering_weight > 1e-4f
			&& is_valid_index(input.forming_steering_offsets, global_index))
		{
			desired_world += (*input.forming_steering_offsets)[static_cast<size_t>(global_index)]
				* input.forming_steering_weight
				* (1.0f - blend_alpha);
		}
	}

	result.valid = true;
	result.world_position = desired_world;
	return result;
}

void ActuationMath::compose_world_positions(
	const ActuationComposeInput& input,
	core::Array<core::Vec3>& out_world_positions)
{
	if (!input.baseline_world_positions)
	{
		out_world_positions.clear();
		return;
	}

	const int32_t particle_count = static_cast<int32_t>(input.baseline_world_positions->size());
	out_world_positions.resize(static_cast<size_t>(particle_count));
	for (int32_t particle_index = 0; particle_index < particle_count; ++particle_index)
	{
		const ActuationComposeResult composed = compose_particle_world_position(input, particle_index);
		out_world_positions[static_cast<size_t>(particle_index)] = composed.valid
			? composed.world_position
			: core::Vec3 {};
	}
}

} // namespace metaagent::particle
