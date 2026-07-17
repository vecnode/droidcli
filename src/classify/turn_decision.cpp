#include "classify/turn_decision.hpp"

#include "intent/create_file_intent.hpp"
#include "intent/create_image_intent.hpp"
#include "intent/open_intent.hpp"
#include "intent/pending_command.hpp"
#include "net/json.hpp"

#include <cstdint>
#include <string>

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

	// "Create an NxN <color> image" - built entirely from a validated
	// integer pair and a whitelisted color name, by code, never as
	// model-authored ffmpeg text. work_dir is omitted so it defaults to the
	// real Desktop (resolve_work_dir_or_desktop, cli/host.cpp).
	const intent::CreateImageIntent create_image = intent::parse_create_image_intent(user_message);
	if (create_image.matched)
	{
		TurnDecision decision;
		decision.kind = TurnDecisionKind::DeterministicTool;
		decision.tool_name = "run_ffmpeg";
		const core::String ffmpeg_args = "-y -f lavfi -i color=" + create_image.color + ":s="
			+ std::to_string(create_image.width) + "x" + std::to_string(create_image.height)
			+ " -frames:v 1 -update 1 " + create_image.file_name;
		decision.arguments_json = "{" + net::json_string_field("args", ffmpeg_args) + "}";
		return decision;
	}

	// "Create a file called X" - an empty write_file call; write_file's own
	// existing guards resolve file_name against the real Desktop.
	const intent::CreateFileIntent create_file = intent::parse_create_file_intent(user_message);
	if (create_file.matched)
	{
		TurnDecision decision;
		decision.kind = TurnDecisionKind::DeterministicTool;
		decision.tool_name = "write_file";
		decision.arguments_json = "{" + net::json_string_field("path", create_file.file_name) + ","
			+ net::json_string_field("content", "") + "}";
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
