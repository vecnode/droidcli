#include "particle/transition_graph.hpp"

#include <algorithm>
#include <vector>

namespace metaagent::particle {
namespace {

bool guard_always(const TransitionContext&) { return true; }
bool guard_not_manual(const TransitionContext& context) { return !context.manual_state_advance; }
bool guard_manual(const TransitionContext& context) { return context.manual_state_advance; }
bool guard_mask_ready(const TransitionContext& context)
{
	return !context.awaiting_async_mask && context.pattern_target_count > 0;
}
bool guard_awaiting_mask(const TransitionContext& context) { return context.awaiting_async_mask; }
bool guard_awaiting_mask_auto_only(const TransitionContext& context)
{
	return context.awaiting_async_mask && !context.manual_state_advance;
}
bool guard_manual_awaiting_mask(const TransitionContext& context)
{
	return context.manual_state_advance && context.awaiting_async_mask;
}
bool guard_form_timeout(const TransitionContext& context)
{
	return !context.manual_state_advance
		&& context.state_elapsed_seconds >= std::max(0.1f, context.form_duration_seconds);
}
bool guard_hold_timeout(const TransitionContext& context)
{
	return !context.manual_state_advance
		&& context.hold_duration_seconds > 0.0f
		&& context.state_elapsed_seconds >= context.hold_duration_seconds;
}
bool guard_return_timeout(const TransitionContext& context)
{
	return context.state_elapsed_seconds >= std::max(0.1f, context.return_duration_seconds);
}
bool guard_dissipate_timeout(const TransitionContext& context)
{
	return !context.manual_state_advance
		&& context.state_elapsed_seconds >= std::max(0.1f, context.dissipate_duration_seconds);
}
bool guard_skip_return_cancel(const TransitionContext& context)
{
	return context.skip_return_on_cancel
		|| context.state == PatternState::Anticipating
		|| context.state == PatternState::Preparing;
}
bool guard_needs_return_cancel(const TransitionContext& context) { return !guard_skip_return_cancel(context); }
bool guard_morph_targets_ready(const TransitionContext& context) { return context.pattern_target_count > 0; }

struct TransitionRow {
	PatternState from = PatternState::Idle;
	TransitionTrigger trigger = TransitionTrigger::Advance;
	TransitionGraph::GuardFn guard;
	TransitionAction action = TransitionAction::None;
	PatternState to_state = PatternState::Idle;
	bool restore_idle_baseline_on_enter = false;
	bool clear_pattern_start_log = false;
};

std::vector<TransitionRow>& rows()
{
	static std::vector<TransitionRow> instance;
	return instance;
}

} // namespace

void TransitionGraph::register_defaults()
{
	if (!rows().empty())
	{
		return;
	}

	auto add = [](TransitionRow row) { rows().push_back(std::move(row)); };

	add({PatternState::Idle, TransitionTrigger::Start, guard_not_manual, TransitionAction::BeginPatternStart, PatternState::Anticipating});
	add({PatternState::Idle, TransitionTrigger::Start, guard_manual, TransitionAction::BeginPatternStart, PatternState::Forming});
	add({PatternState::Idle, TransitionTrigger::Advance, guard_not_manual, TransitionAction::BeginPatternStart, PatternState::Anticipating});
	add({PatternState::Idle, TransitionTrigger::Advance, guard_manual, TransitionAction::BeginPatternStart, PatternState::Forming});
	add({PatternState::Preparing, TransitionTrigger::Timeout, guard_not_manual, TransitionAction::EnterState, PatternState::Anticipating});
	add({PatternState::Preparing, TransitionTrigger::Advance, guard_manual_awaiting_mask, TransitionAction::None, PatternState::Preparing});
	add({PatternState::Preparing, TransitionTrigger::Advance, guard_mask_ready, TransitionAction::EnterState, PatternState::Forming, false, true});
	add({PatternState::Preparing, TransitionTrigger::Cancel, guard_skip_return_cancel, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Preparing, TransitionTrigger::Retreat, guard_always, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Anticipating, TransitionTrigger::Advance, guard_awaiting_mask_auto_only, TransitionAction::None, PatternState::Anticipating});
	add({PatternState::Anticipating, TransitionTrigger::Advance, guard_mask_ready, TransitionAction::EnterState, PatternState::Forming, false, true});
	add({PatternState::Anticipating, TransitionTrigger::Ready,
		[](const TransitionContext& context) { return guard_not_manual(context) && guard_mask_ready(context); },
		TransitionAction::EnterState, PatternState::Forming, false, true});
	add({PatternState::Anticipating, TransitionTrigger::Cancel, guard_skip_return_cancel, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Anticipating, TransitionTrigger::Retreat, guard_always, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Forming, TransitionTrigger::Advance, guard_always, TransitionAction::EnterState, PatternState::Holding});
	add({PatternState::Forming, TransitionTrigger::Timeout, guard_form_timeout, TransitionAction::EnterState, PatternState::Holding});
	add({PatternState::Forming, TransitionTrigger::Retreat, guard_manual, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Forming, TransitionTrigger::Retreat, guard_not_manual, TransitionAction::EnterState, PatternState::Anticipating});
	add({PatternState::Holding, TransitionTrigger::Advance, guard_always, TransitionAction::BeginConfiguredReturn, PatternState::Returning});
	add({PatternState::Holding, TransitionTrigger::Timeout, guard_hold_timeout, TransitionAction::BeginConfiguredReturn, PatternState::Returning});
	add({PatternState::Holding, TransitionTrigger::Morph, guard_morph_targets_ready, TransitionAction::EnterState, PatternState::Forming, false, true});
	add({PatternState::Holding, TransitionTrigger::Retreat, guard_always, TransitionAction::EnterState, PatternState::Forming});
	add({PatternState::Holding, TransitionTrigger::Cancel, guard_needs_return_cancel, TransitionAction::BeginConfiguredReturn, PatternState::Returning});
	add({PatternState::Returning, TransitionTrigger::Advance, guard_always, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Returning, TransitionTrigger::Timeout, guard_return_timeout, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Returning, TransitionTrigger::Retreat, guard_always, TransitionAction::EnterState, PatternState::Holding});
	add({PatternState::Returning, TransitionTrigger::Dissipate, guard_always, TransitionAction::RequestDissipate, PatternState::Dissipating});
	add({PatternState::Dissipating, TransitionTrigger::Advance, guard_always, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Dissipating, TransitionTrigger::Timeout, guard_dissipate_timeout, TransitionAction::CompleteRun, PatternState::Idle});
	add({PatternState::Dissipating, TransitionTrigger::Retreat, guard_always, TransitionAction::EnterState, PatternState::Holding});

	auto add_cancel_skip = [&add](PatternState from_state)
	{
		add({from_state, TransitionTrigger::Cancel,
			[](const TransitionContext& context) { return context.skip_return_on_cancel; },
			TransitionAction::CompleteRun, PatternState::Idle});
	};

	add_cancel_skip(PatternState::Forming);
	add_cancel_skip(PatternState::Holding);
	add_cancel_skip(PatternState::Returning);
	add_cancel_skip(PatternState::Dissipating);

	add({PatternState::Forming, TransitionTrigger::Cancel, guard_needs_return_cancel, TransitionAction::BeginConfiguredReturn, PatternState::Returning});
	add({PatternState::Returning, TransitionTrigger::Cancel, guard_needs_return_cancel, TransitionAction::BeginConfiguredReturn, PatternState::Returning});
	add({PatternState::Dissipating, TransitionTrigger::Cancel, guard_needs_return_cancel, TransitionAction::CompleteRun, PatternState::Idle});
}

void TransitionGraph::register_transition(
	const PatternState from,
	const TransitionTrigger trigger,
	GuardFn guard,
	const TransitionAction action,
	const PatternState to_state,
	const bool restore_idle_baseline_on_enter,
	const bool clear_pattern_start_log)
{
	register_defaults();
	rows().push_back({from, trigger, std::move(guard), action, to_state, restore_idle_baseline_on_enter, clear_pattern_start_log});
}

bool TransitionGraph::evaluate_transition(
	const TransitionContext& context,
	const TransitionTrigger trigger,
	TransitionResult& out_result)
{
	register_defaults();
	out_result = {};

	for (const TransitionRow& row : rows())
	{
		if (row.from != context.state || row.trigger != trigger)
		{
			continue;
		}

		if (row.guard && !row.guard(context))
		{
			continue;
		}

		out_result.handled = true;
		out_result.action = row.action;
		out_result.new_state = row.to_state;
		out_result.restore_idle_baseline_on_enter = row.restore_idle_baseline_on_enter;
		out_result.clear_pattern_start_log = row.clear_pattern_start_log;
		return true;
	}

	return false;
}

} // namespace metaagent::particle
