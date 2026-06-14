#include "particle/visual_continuity.hpp"

#include "particle/scheduler.hpp"

namespace metaagent::particle {
namespace {

void assign_pose_arrays(
	PatternRuntime& runtime,
	const core::Array<core::Vec3>& positions,
	const core::Vec3& center)
{
	runtime.baseline_world_positions = positions;
	runtime.pattern_world_targets = positions;
	runtime.pattern_center = center;
}

} // namespace

void compute_pattern_center(const core::Array<core::Vec3>& positions, core::Vec3& out_center)
{
	out_center = {};
	if (positions.empty())
	{
		return;
	}

	for (const core::Vec3& position : positions)
	{
		out_center.x += position.x;
		out_center.y += position.y;
		out_center.z += position.z;
	}

	const float inverse_count = 1.0f / static_cast<float>(positions.size());
	out_center.x *= inverse_count;
	out_center.y *= inverse_count;
	out_center.z *= inverse_count;
}

void freeze_displayed_pose(ParticleScheduler& scheduler, const DisplayedPose& displayed)
{
	if (displayed.world_positions.empty())
	{
		return;
	}

	core::Vec3 center = {};
	compute_pattern_center(displayed.world_positions, center);
	assign_pose_arrays(scheduler.pattern_runtime, displayed.world_positions, center);
}

void apply_visual_continuity_for_transition(
	ParticleScheduler& scheduler,
	const TransitionResult& result,
	const DisplayedPose& displayed)
{
	if (displayed.world_positions.empty())
	{
		return;
	}

	core::Vec3 center = {};
	compute_pattern_center(displayed.world_positions, center);

	PatternRuntime& runtime = scheduler.pattern_runtime;

	if (result.new_state == PatternState::Holding)
	{
		freeze_displayed_pose(scheduler, displayed);
	}
	else if (result.new_state == PatternState::Forming
		&& !runtime.canonical_pattern_world_targets.empty())
	{
		runtime.baseline_world_positions = displayed.world_positions;
		runtime.pattern_world_targets = runtime.canonical_pattern_world_targets;
		runtime.pattern_center = center;
	}
	else if (result.new_state == PatternState::Preparing)
	{
		freeze_displayed_pose(scheduler, displayed);
	}
	else if (result.new_state == PatternState::Anticipating)
	{
		runtime.baseline_world_positions = displayed.world_positions;
		runtime.pattern_center = center;
		if (!runtime.idle_baseline_world_positions.empty())
		{
			runtime.pattern_world_targets = runtime.idle_baseline_world_positions;
		}
		else
		{
			runtime.pattern_world_targets = displayed.world_positions;
		}
	}
	else if (result.new_state == PatternState::Returning
		|| result.action == TransitionAction::BeginConfiguredReturn)
	{
		runtime.return_hold_positions = displayed.world_positions;
	}
	else if (result.new_state == PatternState::Dissipating
		|| result.action == TransitionAction::RequestDissipate)
	{
		runtime.dissipate_start_positions = displayed.world_positions;
		runtime.baseline_world_positions = displayed.world_positions;
		runtime.pattern_center = center;
	}
	else
	{
		runtime.baseline_world_positions = displayed.world_positions;
		runtime.pattern_center = center;
	}
}

void ParticleScheduler::freeze_displayed_pose(const DisplayedPose& displayed)
{
	if (displayed.world_positions.empty())
	{
		return;
	}

	core::Vec3 center = {};
	compute_pattern_center(displayed.world_positions, center);
	assign_pose_arrays(pattern_runtime, displayed.world_positions, center);
}

} // namespace metaagent::particle
