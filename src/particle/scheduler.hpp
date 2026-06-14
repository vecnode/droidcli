#pragma once

#include <cstdint>
#include <functional>

#include "export.hpp"
#include "particle/pattern_types.hpp"
#include "particle/state_effects.hpp"
#include "particle/representation_types.hpp"
#include "particle/visual_continuity.hpp"
#include "runtime/host_interfaces.hpp"

namespace metaagent::particle {

struct SchedulerCallbacks {
	std::function<bool()> build_pattern_targets;
	std::function<bool()> begin_pattern_start;
	std::function<bool()> begin_configured_return;
	std::function<bool()> request_dissipate_to_center;
	std::function<void()> complete_pattern_run;
	std::function<void(PatternState new_state, PatternState previous_state)> enter_pattern_state;
	std::function<float(PatternState state, float normalized_time_in_state)> evaluate_phase_for_state;
	std::function<void()> commit_anticipation_baseline_for_forming;
	std::function<void(PatternState new_state)> on_transition_side_effects;
	std::function<void(const core::String& message)> log_info;
	std::function<void(const core::String& message)> log_warning;
	runtime::ParticleHostCallbacks particle_host;
};

struct SchedulerSettings {
	bool manual_pattern_state_advance = true;
	float return_release_authority_threshold = 0.05f;
};

class ParticleScheduler {
public:
	PatternConfig pattern_config;
	PatternRuntime pattern_runtime;
	SchedulerSettings settings;
	float last_pattern_tick_delta_seconds = 0.0f;
	float forming_steering_blend_elapsed_seconds = 0.0f;
	bool steering_target_enabled = false;
	core::Array<core::Vec3> forming_steering_offsets;
	float forming_steering_blend_duration_seconds = 0.2f;
	StateEffectStack state_effects;

	METAAGENT_API void reset_pattern_runtime();
	METAAGENT_API TransitionContext build_transition_context(bool skip_return_on_cancel) const;
	METAAGENT_API bool dispatch_pattern_transition(TransitionTrigger trigger, SchedulerCallbacks& callbacks, bool skip_return_on_cancel = false);
	METAAGENT_API bool apply_transition_result(const TransitionResult& result, SchedulerCallbacks& callbacks);
	METAAGENT_API void tick_pattern_runtime(float delta_time_seconds, SchedulerCallbacks& callbacks);
	METAAGENT_API RepresentationFrame build_representation_frame() const;
	METAAGENT_API float get_active_state_duration_seconds() const;
	METAAGENT_API float get_active_state_time_remaining_seconds() const;
	METAAGENT_API const PatternConfig& get_timing_config_for_tick() const;
	METAAGENT_API core::String build_pattern_status_text(int32_t exported_particle_count, int32_t queue_size) const;
	METAAGENT_API core::String build_pattern_timings_text() const;
	METAAGENT_API StateEffectTriggerResult toggle_state_effect(const core::String& effect_id);
	METAAGENT_API void tick_state_effects();
	METAAGENT_API void freeze_displayed_pose(const DisplayedPose& displayed);
};

} // namespace metaagent::particle
