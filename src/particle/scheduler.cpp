#include "particle/scheduler.hpp"

#include "core/math.hpp"
#include "particle/actuation_math.hpp"
#include "particle/representation_types.hpp"
#include "particle/state_effects.hpp"
#include "particle/transition_graph.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace metaagent::particle {
namespace {

core::String join_tags(const core::Array<core::String>& tags)
{
	if (tags.empty())
	{
		return {};
	}

	std::ostringstream stream;
	for (size_t index = 0; index < tags.size(); ++index)
	{
		if (index > 0)
		{
			stream << ", ";
		}
		stream << tags[index];
	}
	return stream.str();
}

float evaluate_phase_for_state_default(const ParticleScheduler& scheduler, const PatternState state, const float normalized_time_in_state)
{
	return ActuationMath::evaluate_phase_for_state(
		state,
		normalized_time_in_state,
		scheduler.pattern_runtime.active_config,
		scheduler.pattern_runtime.form_curve_samples,
		scheduler.pattern_runtime.asset_return_curve_samples);
}

struct SchedulerComposeBundle {
	FormingSettings return_forming_settings;
	ActuationComposeInput input;
};

SchedulerComposeBundle build_compose_bundle_from_scheduler(const ParticleScheduler& scheduler)
{
	const PatternRuntime& runtime = scheduler.pattern_runtime;
	SchedulerComposeBundle bundle;
	ActuationComposeInput& input = bundle.input;
	input.pattern_state = runtime.state;
	input.pattern_center = runtime.pattern_center;
	input.baseline_world_positions = runtime.baseline_world_positions.empty()
		? nullptr
		: &runtime.baseline_world_positions;
	if (!runtime.pattern_world_targets.empty())
	{
		input.pattern_world_targets = &runtime.pattern_world_targets;
	}
	else if (runtime.state == PatternState::Idle && !runtime.baseline_world_positions.empty())
	{
		input.pattern_world_targets = &runtime.baseline_world_positions;
	}
	else
	{
		input.pattern_world_targets = nullptr;
	}
	input.return_hold_positions = runtime.return_hold_positions.empty()
		? nullptr
		: &runtime.return_hold_positions;
	input.return_rest_positions = runtime.return_rest_positions.empty()
		? nullptr
		: &runtime.return_rest_positions;
	input.dissipate_start_positions = runtime.dissipate_start_positions.empty()
		? nullptr
		: &runtime.dissipate_start_positions;
	input.idle_baseline_world_positions = runtime.idle_baseline_world_positions.empty()
		? nullptr
		: &runtime.idle_baseline_world_positions;
	input.forming_steering_offsets = scheduler.forming_steering_offsets.empty()
		? nullptr
		: &scheduler.forming_steering_offsets;

	const bool returning = runtime.state == PatternState::Returning;
	const bool dissipating = runtime.state == PatternState::Dissipating;
	const PatternConfig& config = scheduler.get_timing_config_for_tick();

	input.blend_alpha = ActuationMath::compute_actuation_blend_alpha(runtime.state, runtime.phase);
	if (returning)
	{
		input.blend_alpha = core::math::clamp(runtime.phase, 0.0f, 1.0f);
	}

	input.use_return_hold_blend = returning;
	input.forming_settings = &config.forming;
	input.forming_state_elapsed_seconds = runtime.state_elapsed_seconds;
	input.forming_duration_seconds = std::max(0.1f, config.form_duration_seconds);
	input.forming_delta_time_seconds = scheduler.last_pattern_tick_delta_seconds;

	if (returning)
	{
		const ReturnSettings& return_settings = config.return_settings;
		if (return_settings.uses_motion_solver())
		{
			bundle.return_forming_settings = return_settings.as_forming_settings();
			input.forming_settings = &bundle.return_forming_settings;
		}
		input.forming_duration_seconds = std::max(0.1f, config.return_duration_seconds);
	}

	if (runtime.state == PatternState::Forming
		&& scheduler.steering_target_enabled
		&& scheduler.forming_steering_blend_duration_seconds > core::math::k_epsilon
		&& !scheduler.forming_steering_offsets.empty())
	{
		input.forming_steering_weight = core::math::clamp(
			1.0f - (scheduler.forming_steering_blend_elapsed_seconds / scheduler.forming_steering_blend_duration_seconds),
			0.0f,
			1.0f);
	}

	if (runtime.state == PatternState::Anticipating && !scheduler.settings.manual_pattern_state_advance)
	{
		input.anticipating_motion = true;
		input.anticipation_elapsed_seconds = runtime.state_elapsed_seconds;
		input.anticipation_amplitude_cm = config.anticipation_amplitude_cm;
		input.anticipation_frequency_hz = config.anticipation_frequency_hz;
		input.anticipation_idle_blend_duration_seconds = std::max(
			0.05f,
			config.anticipation_idle_blend_duration_seconds);
		// Soft bridge: keep idle rest pose + ambient breathing until Forming begins.
		input.blend_alpha = 0.0f;
	}
	else if (dissipating)
	{
		input.dissipating_motion = true;
		input.blend_alpha = runtime.phase;
	}
	else if (runtime.state == PatternState::Forming && runtime.anticipation_handoff_elapsed_seconds >= 0.0f
		&& !runtime.idle_baseline_world_positions.empty())
	{
		input.anticipation_handoff_elapsed_seconds = runtime.anticipation_handoff_elapsed_seconds;
		input.anticipation_amplitude_cm = config.anticipation_amplitude_cm;
		input.anticipation_frequency_hz = config.anticipation_frequency_hz;
		input.anticipation_idle_blend_duration_seconds = std::max(
			0.05f,
			config.anticipation_idle_blend_duration_seconds);
		input.forming_anticipation_carryover_duration_seconds = std::max(
			0.05f,
			config.forming_anticipation_carryover_duration_seconds);
	}

	return bundle;
}

void tick_state_effect_layer(ParticleScheduler& scheduler)
{
	if (scheduler.pattern_runtime.baseline_world_positions.empty())
	{
		return;
	}

	const float delta_time = scheduler.last_pattern_tick_delta_seconds > core::math::k_epsilon
		? scheduler.last_pattern_tick_delta_seconds
		: (1.0f / 60.0f);

	const SchedulerComposeBundle bundle = build_compose_bundle_from_scheduler(scheduler);
	ActuationComposeInput compose_input = bundle.input;
	compose_input.blend_alpha = ActuationMath::compute_actuation_blend_alpha(
		scheduler.pattern_runtime.state,
		scheduler.pattern_runtime.phase);

	core::Array<core::Vec3> composed;
	ActuationMath::compose_world_positions(compose_input, composed);
	if (composed.empty())
	{
		return;
	}

	StateEffectTickContext context;
	context.pattern_state = scheduler.pattern_runtime.state;
	context.pattern_center = scheduler.pattern_runtime.pattern_center;
	context.state_elapsed_seconds = scheduler.pattern_runtime.state_elapsed_seconds;
	context.delta_time_seconds = delta_time;
	context.composed_positions = &composed;
	scheduler.state_effects.tick(context);
}

bool should_capture_visual_continuity(const TransitionResult& result)
{
	switch (result.action)
	{
	case TransitionAction::None:
	case TransitionAction::CompleteRun:
	case TransitionAction::BeginPatternStart:
		return false;
	default:
		return true;
	}
}

void capture_visual_continuity_for_transition(
	ParticleScheduler& scheduler,
	const TransitionResult& result,
	const SchedulerCallbacks& callbacks)
{
	DisplayedPose displayed;
	if (callbacks.particle_host.read_displayed_positions
		&& callbacks.particle_host.read_displayed_positions(displayed))
	{
		apply_visual_continuity_for_transition(scheduler, result, displayed);
		if (callbacks.particle_host.apply_world_positions)
		{
			callbacks.particle_host.apply_world_positions(displayed.world_positions);
		}
		return;
	}

	if (scheduler.pattern_runtime.baseline_world_positions.empty())
	{
		return;
	}

	const SchedulerComposeBundle bundle = build_compose_bundle_from_scheduler(scheduler);
	core::Array<core::Vec3> composed;
	ActuationMath::compose_world_positions(bundle.input, composed);
	if (composed.empty())
	{
		return;
	}

	displayed.world_positions = composed;
	compute_pattern_center(composed, displayed.pattern_center);
	apply_visual_continuity_for_transition(scheduler, result, displayed);
}

void enter_state(ParticleScheduler& scheduler, const PatternState new_state, SchedulerCallbacks& callbacks)
{
	const PatternState previous_state = scheduler.pattern_runtime.state;

	const bool forming_from_anticipating =
		new_state == PatternState::Forming && previous_state == PatternState::Anticipating;

	if (forming_from_anticipating && callbacks.commit_anticipation_baseline_for_forming)
	{
		callbacks.commit_anticipation_baseline_for_forming();
	}
	else if (new_state == PatternState::Forming)
	{
		scheduler.pattern_runtime.anticipation_handoff_elapsed_seconds = -1.0f;
	}

	scheduler.pattern_runtime.state = new_state;
	scheduler.pattern_runtime.state_elapsed_seconds = 0.0f;

	if (new_state == PatternState::Anticipating)
	{
		scheduler.pattern_runtime.phase = 0.0f;
	}
	else if (new_state == PatternState::Forming)
	{
		scheduler.pattern_runtime.phase = 0.0f;
		scheduler.forming_steering_blend_elapsed_seconds = 0.0f;
	}
	else if (new_state == PatternState::Holding)
	{
		scheduler.pattern_runtime.phase = 1.0f;
	}
	else if (new_state == PatternState::Returning)
	{
		scheduler.pattern_runtime.phase = 1.0f;
	}
	else if (new_state == PatternState::Dissipating)
	{
		scheduler.pattern_runtime.phase = 0.0f;
	}

	if (callbacks.enter_pattern_state)
	{
		callbacks.enter_pattern_state(new_state, previous_state);
	}
	if (callbacks.on_transition_side_effects)
	{
		callbacks.on_transition_side_effects(new_state);
	}
}

void tick_async_mask_prepare_state(ParticleScheduler& scheduler, SchedulerCallbacks& callbacks)
{
	scheduler.pattern_runtime.phase = 0.0f;

	if (!callbacks.build_pattern_targets
		|| (!scheduler.pattern_runtime.awaiting_async_mask
			&& !scheduler.pattern_runtime.pattern_world_targets.empty()))
	{
		return;
	}

	const bool build_success = callbacks.build_pattern_targets();
	if (!build_success
		&& !scheduler.settings.manual_pattern_state_advance
		&& !scheduler.pattern_runtime.awaiting_async_mask
		&& scheduler.pattern_runtime.pattern_world_targets.empty()
		&& scheduler.pattern_runtime.state_elapsed_seconds > 0.25f)
	{
		if (callbacks.log_warning)
		{
			callbacks.log_warning(
				"ParticleScheduler: async mask build failed during prepare; completing run.");
		}
		if (callbacks.complete_pattern_run)
		{
			callbacks.complete_pattern_run();
		}
		return;
	}

	if (!scheduler.settings.manual_pattern_state_advance && scheduler.pattern_runtime.state_elapsed_seconds > 60.0f)
	{
		if (callbacks.log_warning)
		{
			callbacks.log_warning(
				"ParticleScheduler: async mask build timed out after 60s; completing run.");
		}
		if (callbacks.complete_pattern_run)
		{
			callbacks.complete_pattern_run();
		}
		return;
	}

	if (!scheduler.settings.manual_pattern_state_advance
		&& !scheduler.pattern_runtime.awaiting_async_mask
		&& !scheduler.pattern_runtime.pattern_world_targets.empty())
	{
		scheduler.dispatch_pattern_transition(TransitionTrigger::Ready, callbacks);
	}
}

} // namespace

void ParticleScheduler::reset_pattern_runtime()
{
	pattern_runtime = {};
	last_pattern_tick_delta_seconds = 0.0f;
	forming_steering_blend_elapsed_seconds = 0.0f;
	steering_target_enabled = false;
	forming_steering_offsets.clear();
	state_effects.reset();
}

TransitionContext ParticleScheduler::build_transition_context(const bool skip_return_on_cancel) const
{
	TransitionContext context;
	context.state = pattern_runtime.state;
	context.awaiting_async_mask = pattern_runtime.awaiting_async_mask;
	context.pattern_target_count = static_cast<int32_t>(pattern_runtime.pattern_world_targets.size());
	context.manual_state_advance = settings.manual_pattern_state_advance;
	context.skip_return_on_cancel = skip_return_on_cancel;
	context.state_elapsed_seconds = pattern_runtime.state_elapsed_seconds;

	const PatternConfig& timings = get_timing_config_for_tick();
	context.form_duration_seconds = timings.form_duration_seconds;
	context.hold_duration_seconds = timings.hold_duration_seconds;
	context.return_duration_seconds = timings.return_duration_seconds;
	context.dissipate_duration_seconds = timings.dissipate_duration_seconds;
	context.dissipate_return_mode = timings.return_settings.mode == ReturnMode::DissipateToCenter;
	return context;
}

bool ParticleScheduler::dispatch_pattern_transition(
	const TransitionTrigger trigger,
	SchedulerCallbacks& callbacks,
	const bool skip_return_on_cancel)
{
	TransitionResult result;
	if (!TransitionGraph::evaluate_transition(
		build_transition_context(skip_return_on_cancel),
		trigger,
		result))
	{
		return false;
	}

	if (should_capture_visual_continuity(result))
	{
		capture_visual_continuity_for_transition(*this, result, callbacks);
	}

	return apply_transition_result(result, callbacks);
}

bool ParticleScheduler::apply_transition_result(const TransitionResult& result, SchedulerCallbacks& callbacks)
{
	if (!result.handled)
	{
		return false;
	}

	switch (result.action)
	{
	case TransitionAction::None:
		return false;
	case TransitionAction::BeginPatternStart:
		if (settings.manual_pattern_state_advance && pattern_runtime.state == PatternState::Idle)
		{
			TransitionResult continuity_result;
			continuity_result.new_state = PatternState::Preparing;
			continuity_result.action = TransitionAction::EnterState;
			capture_visual_continuity_for_transition(*this, continuity_result, callbacks);
		}
		if (!callbacks.begin_pattern_start || !callbacks.begin_pattern_start())
		{
			return false;
		}
		if (settings.manual_pattern_state_advance)
		{
			if (pattern_runtime.awaiting_async_mask)
			{
				enter_state(*this, PatternState::Preparing, callbacks);
			}
			else
			{
				enter_state(*this, PatternState::Forming, callbacks);
			}
		}
		else
		{
			enter_state(*this, PatternState::Anticipating, callbacks);
		}
		return true;
	case TransitionAction::CompleteRun:
		if (callbacks.complete_pattern_run)
		{
			callbacks.complete_pattern_run();
		}
		return true;
	case TransitionAction::BeginConfiguredReturn:
		return callbacks.begin_configured_return && callbacks.begin_configured_return();
	case TransitionAction::RequestDissipate:
		return callbacks.request_dissipate_to_center ? callbacks.request_dissipate_to_center() : false;
	case TransitionAction::EnterState:
		enter_state(*this, result.new_state, callbacks);
		return true;
	default:
		return false;
	}
}

void ParticleScheduler::tick_pattern_runtime(const float delta_time_seconds, SchedulerCallbacks& callbacks)
{
	last_pattern_tick_delta_seconds = std::max(0.0f, delta_time_seconds);

	if (pattern_runtime.state != PatternState::Idle)
	{
		pattern_runtime.state_elapsed_seconds += last_pattern_tick_delta_seconds;

		const PatternConfig& timings = get_timing_config_for_tick();

		switch (pattern_runtime.state)
	{
	case PatternState::Preparing:
		tick_async_mask_prepare_state(*this, callbacks);
		if (!settings.manual_pattern_state_advance)
		{
			dispatch_pattern_transition(TransitionTrigger::Timeout, callbacks);
		}
		break;

	case PatternState::Anticipating:
		tick_async_mask_prepare_state(*this, callbacks);
		break;

	case PatternState::Forming:
	{
		forming_steering_blend_elapsed_seconds += std::max(0.0f, delta_time_seconds);
		const float form_duration = std::max(0.1f, timings.form_duration_seconds);
		const float normalized_time = core::math::clamp(
			pattern_runtime.state_elapsed_seconds / form_duration,
			0.0f,
			1.0f);
		const float evaluated_phase = callbacks.evaluate_phase_for_state
			? callbacks.evaluate_phase_for_state(PatternState::Forming, normalized_time)
			: evaluate_phase_for_state_default(*this, PatternState::Forming, normalized_time);
		pattern_runtime.phase = core::math::clamp(evaluated_phase, 0.0f, 1.0f);

		if (settings.manual_pattern_state_advance && pattern_runtime.state_elapsed_seconds >= form_duration)
		{
			pattern_runtime.state_elapsed_seconds = form_duration;
			pattern_runtime.phase = 1.0f;
		}
		else if (!settings.manual_pattern_state_advance && pattern_runtime.state_elapsed_seconds >= form_duration)
		{
			dispatch_pattern_transition(TransitionTrigger::Timeout, callbacks);
		}
		break;
	}

	case PatternState::Holding:
	{
		pattern_runtime.phase = 1.0f;
		const float hold_duration = std::max(0.0f, timings.hold_duration_seconds);
		if (settings.manual_pattern_state_advance
			&& hold_duration > 0.0f
			&& pattern_runtime.state_elapsed_seconds >= hold_duration)
		{
			pattern_runtime.state_elapsed_seconds = hold_duration;
		}
		else if (!settings.manual_pattern_state_advance
			&& pattern_runtime.state_elapsed_seconds >= hold_duration)
		{
			dispatch_pattern_transition(TransitionTrigger::Timeout, callbacks);
		}
		break;
	}

	case PatternState::Returning:
	{
		const float return_duration = std::max(0.1f, timings.return_duration_seconds);
		const float normalized_time = core::math::clamp(
			pattern_runtime.state_elapsed_seconds / return_duration,
			0.0f,
			1.0f);
		const float evaluated_phase = callbacks.evaluate_phase_for_state
			? callbacks.evaluate_phase_for_state(PatternState::Returning, normalized_time)
			: evaluate_phase_for_state_default(*this, PatternState::Returning, normalized_time);
		pattern_runtime.phase = core::math::clamp(evaluated_phase, 0.0f, 1.0f);

		if (settings.manual_pattern_state_advance && pattern_runtime.state_elapsed_seconds >= return_duration)
		{
			pattern_runtime.state_elapsed_seconds = return_duration;
			pattern_runtime.phase = 0.0f;
		}
		else if (!settings.manual_pattern_state_advance && pattern_runtime.state_elapsed_seconds >= return_duration)
		{
			pattern_runtime.state_elapsed_seconds = return_duration;
			pattern_runtime.phase = 0.0f;
			dispatch_pattern_transition(TransitionTrigger::Timeout, callbacks);
		}
		break;
	}

	case PatternState::Dissipating:
	{
		const float dissipate_duration = std::max(0.1f, timings.dissipate_duration_seconds);
		const float normalized_time = core::math::clamp(
			pattern_runtime.state_elapsed_seconds / dissipate_duration,
			0.0f,
			1.0f);
		pattern_runtime.phase = core::math::smooth_step01(normalized_time);

		if (settings.manual_pattern_state_advance && pattern_runtime.state_elapsed_seconds >= dissipate_duration)
		{
			pattern_runtime.state_elapsed_seconds = dissipate_duration;
			pattern_runtime.phase = 1.0f;
		}
		else if (!settings.manual_pattern_state_advance && pattern_runtime.state_elapsed_seconds >= dissipate_duration)
		{
			dispatch_pattern_transition(TransitionTrigger::Timeout, callbacks);
		}
		break;
	}

	case PatternState::Idle:
	case PatternState::Releasing:
	default:
		break;
	}
	}

	tick_state_effect_layer(*this);
}

RepresentationFrame ParticleScheduler::build_representation_frame() const
{
	RepresentationFrame out_frame;
	out_frame.pattern_state = pattern_runtime.state;
	out_frame.macro_phase = RepresentationMapping::macro_phase_from_pattern_state(pattern_runtime.state);
	out_frame.pattern_center = pattern_runtime.pattern_center;
	out_frame.pattern_active = pattern_runtime.state != PatternState::Idle
		&& pattern_runtime.state != PatternState::Preparing;

	out_frame.baseline_world_positions = pattern_runtime.baseline_world_positions;
	out_frame.pattern_world_targets = pattern_runtime.pattern_world_targets;
	if (out_frame.pattern_world_targets.empty()
		&& (pattern_runtime.state == PatternState::Idle
			|| pattern_runtime.state == PatternState::Preparing)
		&& !pattern_runtime.baseline_world_positions.empty())
	{
		out_frame.pattern_world_targets = pattern_runtime.baseline_world_positions;
	}
	out_frame.idle_baseline_world_positions = pattern_runtime.idle_baseline_world_positions;
	out_frame.return_hold_positions = pattern_runtime.return_hold_positions;
	out_frame.return_rest_positions = pattern_runtime.return_rest_positions;
	out_frame.dissipate_start_positions = pattern_runtime.dissipate_start_positions;

	const bool returning = pattern_runtime.state == PatternState::Returning;
	const bool dissipating = pattern_runtime.state == PatternState::Dissipating;

	float blend_alpha = ActuationMath::compute_actuation_blend_alpha(pattern_runtime.state, pattern_runtime.phase);
	if (returning)
	{
		blend_alpha = core::math::clamp(pattern_runtime.phase, 0.0f, 1.0f);
	}

	const PatternConfig& hold_config = get_timing_config_for_tick();
	const float hold_pulse_scale = pattern_runtime.state == PatternState::Holding
		? (1.0f + std::sin(
			pattern_runtime.state_elapsed_seconds * core::math::k_two_pi
			* std::max(0.1f, hold_config.hold_pulse_frequency_hz))
			* core::math::clamp(hold_config.hold_pulse_amplitude, 0.0f, 1.0f))
		: 1.0f;

	out_frame.use_return_hold_blend = returning;
	out_frame.forming_settings = pattern_runtime.active_config.forming;
	out_frame.forming_state_elapsed_seconds = pattern_runtime.state_elapsed_seconds;
	out_frame.forming_duration_seconds = std::max(0.1f, pattern_runtime.active_config.form_duration_seconds);
	out_frame.forming_delta_time_seconds = last_pattern_tick_delta_seconds;

	if (returning)
	{
		const ReturnSettings& return_settings = pattern_runtime.active_config.return_settings;
		out_frame.return_uses_motion_solver = return_settings.uses_motion_solver();
		if (out_frame.return_uses_motion_solver)
		{
			out_frame.return_motion_settings = return_settings.as_forming_settings();
		}
		out_frame.forming_duration_seconds = std::max(0.1f, pattern_runtime.active_config.return_duration_seconds);
	}

	if (pattern_runtime.state == PatternState::Forming
		&& steering_target_enabled
		&& forming_steering_blend_duration_seconds > core::math::k_epsilon
		&& !forming_steering_offsets.empty())
	{
		out_frame.forming_steering_weight = core::math::clamp(
			1.0f - (forming_steering_blend_elapsed_seconds / forming_steering_blend_duration_seconds),
			0.0f,
			1.0f);
		if (out_frame.forming_steering_weight > core::math::k_epsilon)
		{
			out_frame.forming_steering_offsets = forming_steering_offsets;
		}
	}

	if (pattern_runtime.state == PatternState::Anticipating && !settings.manual_pattern_state_advance)
	{
		out_frame.anticipating_motion = true;
		out_frame.anticipation_elapsed_seconds = pattern_runtime.state_elapsed_seconds;
		out_frame.anticipation_amplitude_cm = pattern_runtime.active_config.anticipation_amplitude_cm;
		out_frame.anticipation_frequency_hz = pattern_runtime.active_config.anticipation_frequency_hz;
		out_frame.anticipation_idle_blend_duration_seconds = std::max(
			0.05f,
			pattern_runtime.active_config.anticipation_idle_blend_duration_seconds);
		blend_alpha = 0.0f;
	}
	else if (dissipating)
	{
		out_frame.dissipating_motion = true;
		out_frame.dissipate_visibility = core::math::clamp(1.0f - pattern_runtime.phase, 0.0f, 1.0f);
		blend_alpha = pattern_runtime.phase;
	}
	else if (pattern_runtime.state == PatternState::Forming
		&& pattern_runtime.anticipation_handoff_elapsed_seconds >= 0.0f
		&& !pattern_runtime.idle_baseline_world_positions.empty())
	{
		out_frame.anticipation_handoff_elapsed_seconds = pattern_runtime.anticipation_handoff_elapsed_seconds;
		out_frame.anticipation_amplitude_cm = pattern_runtime.active_config.anticipation_amplitude_cm;
		out_frame.anticipation_frequency_hz = pattern_runtime.active_config.anticipation_frequency_hz;
		out_frame.anticipation_idle_blend_duration_seconds = std::max(
			0.05f,
			pattern_runtime.active_config.anticipation_idle_blend_duration_seconds);
		out_frame.forming_anticipation_carryover_duration_seconds = std::max(
			0.05f,
			pattern_runtime.active_config.forming_anticipation_carryover_duration_seconds);
	}

	const float state_duration = get_active_state_duration_seconds();
	out_frame.phase.normalized_time = state_duration > core::math::k_epsilon
		? core::math::clamp(pattern_runtime.state_elapsed_seconds / state_duration, 0.0f, 1.0f)
		: 0.0f;
	out_frame.phase.blend_alpha = blend_alpha;
	out_frame.phase.emphasis = (dissipating ? out_frame.dissipate_visibility : hold_pulse_scale)
		* state_effects.emphasis_multiplier();
	out_frame.phase.visibility = dissipating ? out_frame.dissipate_visibility : 1.0f;
	out_frame.phase.authority_weight =
		(returning && blend_alpha <= settings.return_release_authority_threshold) ? 0.0f : 1.0f;
	out_frame.state_effect_offsets = state_effects.offsets();

	return out_frame;
}

StateEffectTriggerResult ParticleScheduler::toggle_state_effect(const core::String& effect_id)
{
	return state_effects.toggle(effect_id, pattern_runtime.state);
}

void ParticleScheduler::tick_state_effects()
{
	tick_state_effect_layer(*this);
}

float ParticleScheduler::get_active_state_duration_seconds() const
{
	const PatternConfig& timings = get_timing_config_for_tick();

	switch (pattern_runtime.state)
	{
	case PatternState::Preparing:
		return 60.0f;
	case PatternState::Anticipating:
		return 0.0f;
	case PatternState::Forming:
		return std::max(0.1f, timings.form_duration_seconds);
	case PatternState::Holding:
		return std::max(0.0f, timings.hold_duration_seconds);
	case PatternState::Returning:
		return std::max(0.1f, timings.return_duration_seconds);
	case PatternState::Dissipating:
		return std::max(0.1f, timings.dissipate_duration_seconds);
	case PatternState::Idle:
	case PatternState::Releasing:
	default:
		return 0.0f;
	}
}

float ParticleScheduler::get_active_state_time_remaining_seconds() const
{
	if (pattern_runtime.state == PatternState::Idle)
	{
		return 0.0f;
	}

	return std::max(0.0f, get_active_state_duration_seconds() - pattern_runtime.state_elapsed_seconds);
}

const PatternConfig& ParticleScheduler::get_timing_config_for_tick() const
{
	return pattern_runtime.state == PatternState::Idle
		? pattern_config
		: pattern_runtime.active_config;
}

core::String ParticleScheduler::build_pattern_status_text(const int32_t exported_particle_count, const int32_t queue_size) const
{
	const int32_t particle_count = static_cast<int32_t>(pattern_runtime.baseline_world_positions.size());
	const core::String tags_joined = join_tags(pattern_runtime.active_pattern_tags);
	const core::String tags_suffix = tags_joined.empty() ? core::String() : " | Tags: " + tags_joined;

	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2);

	if (pattern_runtime.state == PatternState::Idle)
	{
		stream
			<< "Pattern State: Idle | Phase: 0.00 | Queue: "
			<< queue_size
			<< " | Particles: "
			<< exported_particle_count
			<< tags_suffix;
		return stream.str();
	}

	const float state_duration = get_active_state_duration_seconds();
	const float time_remaining = get_active_state_time_remaining_seconds();

	stream
		<< "Pattern State: "
		<< RepresentationMapping::pattern_state_to_string(pattern_runtime.state)
		<< " | Macro: "
		<< RepresentationMapping::get_macro_phase_display_name(
			RepresentationMapping::macro_phase_from_pattern_state(pattern_runtime.state))
		<< " | Phase: "
		<< core::math::clamp(pattern_runtime.phase, 0.0f, 1.0f)
		<< " | In-state: "
		<< pattern_runtime.state_elapsed_seconds
		<< "s / "
		<< std::setprecision(1)
		<< state_duration
		<< "s ("
		<< time_remaining
		<< "s left) | Particles: "
		<< particle_count
		<< tags_suffix;
	return stream.str();
}

core::String ParticleScheduler::build_pattern_timings_text() const
{
	const PatternConfig& display_config = get_timing_config_for_tick();

	std::ostringstream stream;
	stream
		<< std::fixed
		<< std::setprecision(1)
		<< "Pattern Preset: "
		<< display_config.get_preset_display_name()
		<< " | Form="
		<< display_config.form_duration_seconds
		<< "s Hold="
		<< display_config.hold_duration_seconds
		<< "s Return="
		<< display_config.return_duration_seconds
		<< "s | Forming="
		<< display_config.forming.get_mode_display_name()
		<< " | Returning="
		<< display_config.return_settings.get_mode_display_name();
	return stream.str();
}

} // namespace metaagent::particle
