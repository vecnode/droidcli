#include "classify/turn_decision.hpp"

#include "intent/open_intent.hpp"
#include "intent/pending_command.hpp"
#include "net/json.hpp"

namespace droidcli::classify {

TurnDecision try_deterministic_classify(
	const core::String& user_message,
	const core::String& previous_assistant_text)
{
	const intent::OpenIntent open_intent = intent::parse_open_intent(user_message);
	if (open_intent.matched)
	{
		TurnDecision decision;
		decision.kind = TurnDecisionKind::DeterministicTool;
		decision.tool_name = "open_application";
		decision.arguments_json = "{" + net::json_string_field("path_or_name", open_intent.app_name) + "}";
		return decision;
	}

	const intent::PendingCommand proposed_command = intent::extract_proposed_command(previous_assistant_text);
	if (proposed_command.matched && intent::is_bare_affirmative(user_message))
	{
		TurnDecision decision;
		decision.kind = TurnDecisionKind::DeterministicTool;
		decision.tool_name = proposed_command.tool;
		decision.arguments_json = proposed_command.tool == "run_ffmpeg"
			? ("{" + net::json_string_field("args", proposed_command.args) + "}")
			: ("{" + net::json_string_field("command", proposed_command.args) + "}");
		decision.already_approved = true;
		return decision;
	}

	return TurnDecision{};
}

} // namespace droidcli::classify
