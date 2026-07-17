#include "classify/turn_decision.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::classify;

	// "open X" phrasing maps straight to an open_application decision, raw
	// app_name unresolved - resolution is the execution pipeline's job.
	{
		const TurnDecision decision = try_deterministic_classify("Open Notepad", "");
		assert(decision.kind == TurnDecisionKind::DeterministicTool);
		assert(decision.tool_name == "open_application");
		assert(decision.arguments_json == "{\"path_or_name\":\"Notepad\"}");
		// A brand-new request, not a prior proposal being confirmed - still
		// goes through the normal approval gate, unlike the pending_command
		// cases below.
		assert(!decision.already_approved);
	}

	// A proposed run_command, confirmed with a bare "yes".
	{
		const TurnDecision decision = try_deterministic_classify(
			"yes",
			"I'll run this:\n```\necho hello\n```\nWould you like me to execute this?");
		assert(decision.kind == TurnDecisionKind::DeterministicTool);
		assert(decision.tool_name == "run_command");
		assert(decision.arguments_json == "{\"command\":\"echo hello\"}");
		// The user's "yes" to the assistant's own proposal already IS the
		// approval - this bypasses tool_call_requires_approval()'s gate.
		assert(decision.already_approved);
	}

	// A proposed run_ffmpeg, confirmed with a bare "yes".
	{
		const TurnDecision decision = try_deterministic_classify(
			"sure",
			"I'll run this:\n```\nffmpeg -i in.mp4 out.mp4\n```\nShould I run this?");
		assert(decision.kind == TurnDecisionKind::DeterministicTool);
		assert(decision.tool_name == "run_ffmpeg");
		assert(decision.arguments_json == "{\"args\":\"-i in.mp4 out.mp4\"}");
		assert(decision.already_approved);
	}

	// A proposed command exists, but the reply isn't a bare affirmative -
	// must not match (same false-positive caution as the underlying
	// recognizers themselves).
	{
		const TurnDecision decision = try_deterministic_classify(
			"yes but change the output name first",
			"I'll run this:\n```\necho hello\n```\nWould you like me to execute this?");
		assert(decision.kind == TurnDecisionKind::NoDeterministicMatch);
	}

	// Unrelated message, no previous proposal - neither recognizer fires.
	{
		const TurnDecision decision = try_deterministic_classify("what's the weather like today", "");
		assert(decision.kind == TurnDecisionKind::NoDeterministicMatch);
		assert(decision.tool_name.empty());
		assert(decision.arguments_json.empty());
	}

	std::cout << "turn_decision_test passed" << std::endl;
	return 0;
}
