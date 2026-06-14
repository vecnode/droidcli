#include "particle/transition_graph.hpp"

#include <cassert>
#include <iostream>

using namespace metaagent::particle;

int main()
{
	TransitionGraph::register_defaults();

	TransitionContext context;
	context.state = PatternState::Idle;
	context.manual_state_advance = true;

	TransitionResult result;
	const bool handled = TransitionGraph::evaluate_transition(context, TransitionTrigger::Advance, result);
	assert(handled);
	assert(result.action == TransitionAction::BeginPatternStart);
	assert(result.new_state == PatternState::Forming);

	context.state = PatternState::Preparing;
	context.awaiting_async_mask = true;
	const bool manual_forming_while_awaiting = TransitionGraph::evaluate_transition(
		context,
		TransitionTrigger::Advance,
		result);
	assert(manual_forming_while_awaiting);
	assert(result.action == TransitionAction::None);
	assert(result.new_state == PatternState::Preparing);

	context.manual_state_advance = false;
	const bool blocked = TransitionGraph::evaluate_transition(context, TransitionTrigger::Advance, result);
	assert(blocked);
	assert(result.action == TransitionAction::None);
	assert(result.new_state == PatternState::Preparing);

	context.manual_state_advance = true;
	context.awaiting_async_mask = false;
	context.pattern_target_count = 128;
	const bool forming = TransitionGraph::evaluate_transition(context, TransitionTrigger::Advance, result);
	assert(forming);
	assert(result.new_state == PatternState::Forming);

	std::cout << "metaagent transition graph tests passed\n";
	return 0;
}
