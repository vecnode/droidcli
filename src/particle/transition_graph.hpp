#pragma once

#include <functional>

#include "export.hpp"
#include "particle/representation_types.hpp"

namespace metaagent::particle {

class TransitionGraph {
public:
	using GuardFn = std::function<bool(const TransitionContext&)>;

	METAAGENT_API static void register_defaults();
	METAAGENT_API static void register_transition(
		PatternState from,
		TransitionTrigger trigger,
		GuardFn guard,
		TransitionAction action,
		PatternState to_state = PatternState::Idle,
		bool restore_idle_baseline_on_enter = false,
		bool clear_pattern_start_log = false);

	METAAGENT_API static bool evaluate_transition(
		const TransitionContext& context,
		TransitionTrigger trigger,
		TransitionResult& out_result);
};

} // namespace metaagent::particle
