#pragma once

#include "core/types.hpp"
#include "export.hpp"
#include "particle/pattern_types.hpp"
#include "particle/transition_graph.hpp"

namespace metaagent::particle {

class ParticleScheduler;

struct DisplayedPose {
	core::Array<core::Vec3> world_positions;
	core::Vec3 pattern_center;
};

METAAGENT_API void compute_pattern_center(const core::Array<core::Vec3>& positions, core::Vec3& out_center);

/** Sets baseline and targets to the displayed pose; updates pattern center. */
METAAGENT_API void freeze_displayed_pose(ParticleScheduler& scheduler, const DisplayedPose& displayed);

/** Applies transition-specific continuity using the displayed pose (what the host actually shows). */
METAAGENT_API void apply_visual_continuity_for_transition(
	ParticleScheduler& scheduler,
	const TransitionResult& result,
	const DisplayedPose& displayed);

} // namespace metaagent::particle
