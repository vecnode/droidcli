#include "net/handlers.hpp"

#include "ai/language_runtime.hpp"
#include "net/json.hpp"
#include "notify/parse.hpp"

namespace droidcli::net {
namespace {

core::String query_param(const core::String& query_string, const core::String& key)
{
	if (query_string.empty())
	{
		return {};
	}

	size_t cursor = 0;
	while (cursor < query_string.size())
	{
		const size_t amp = query_string.find('&', cursor);
		const core::String pair = query_string.substr(
			cursor,
			amp == core::String::npos ? core::String::npos : amp - cursor);

		const size_t equal = pair.find('=');
		if (equal != core::String::npos)
		{
			const core::String pair_key = pair.substr(0, equal);
			if (pair_key == key)
			{
				return pair.substr(equal + 1);
			}
		}
		else if (pair == key)
		{
			return {};
		}

		if (amp == core::String::npos)
		{
			break;
		}

		cursor = amp + 1;
	}

	return {};
}

core::String extract_prompt_field(const core::String& body)
{
	for (const core::String& field_name : {"prompt", "text", "message"})
	{
		const core::String value = extract_json_string_field(body, field_name);
		if (!value.empty())
		{
			return value;
		}
	}
	return {};
}

} // namespace

HttpResponse handle_health(const HandlerContext& context)
{
	HttpResponse response;
	response.body = "{"
		+ json_string_field("status", "ok") + ","
		+ json_string_field("map", context.session.map_name) + ","
		+ json_string_field("build", context.session.build_label)
		+ "}";
	return response;
}

HttpResponse handle_echo(const HttpRequest& request)
{
	core::String echo_text = query_param(request.query_string, "msg");
	if (echo_text.empty() && !request.body.empty())
	{
		echo_text = request.body;
	}

	HttpResponse response;
	response.body = "{" + json_string_field("echo", echo_text) + "}";
	return response;
}

NotifyHandleResult handle_notify(const HttpRequest& request)
{
	NotifyHandleResult result;
	const notify::NotifyParseResult parsed = notify::parse_notify_body(request.body);
	result.response.body = "{" + json_bool_field("ok", parsed.success) + "}";
	result.has_notify_message = parsed.success;
	result.notify_message = parsed.message;
	return result;
}

AiChatHandleResult handle_ai_chat(const HttpRequest& request, const HandlerContext& context)
{
	AiChatHandleResult result;
	result.response.content_type = "application/json";

	if (!context.language_ai || !context.language_ai_transport)
	{
		result.response.status = HttpStatus::InternalError;
		result.response.body = "{"
			+ json_bool_field("ok", false) + ","
			+ json_string_field("error", "Language AI runtime is not available on this host.")
			+ "}";
		return result;
	}

	const core::String prompt = extract_prompt_field(request.body);
	if (prompt.empty())
	{
		result.response.status = HttpStatus::BadRequest;
		result.response.body = "{"
			+ json_bool_field("ok", false) + ","
			+ json_string_field("error", "Missing prompt. Send JSON: {\"prompt\":\"...\"}")
			+ "}";
		return result;
	}

	bool clear_transcript = false;
	extract_json_bool_field(request.body, "clear", clear_transcript);
	if (clear_transcript)
	{
		context.language_ai->clear_transcript();
	}

	const core::String system_prompt = extract_json_string_field(request.body, "system");
	if (!system_prompt.empty())
	{
		context.language_ai->set_system_prompt(system_prompt);
	}

	if (!context.language_ai->submit_user_message(prompt))
	{
		result.response.status = HttpStatus::BadRequest;
		result.response.body = "{"
			+ json_bool_field("ok", false) + ","
			+ json_string_field("error", context.language_ai->snapshot().status_text)
			+ "}";
		return result;
	}

	if (!context.language_ai->complete_turn(*context.language_ai_transport))
	{
		result.response.status = HttpStatus::InternalError;
		result.response.body = "{"
			+ json_bool_field("ok", false) + ","
			+ json_string_field("error", context.language_ai->snapshot().status_text)
			+ "}";
		return result;
	}

	const ai::LanguageSnapshot snapshot = context.language_ai->snapshot();
	result.completed_turn = true;
	result.response.body = "{"
		+ json_bool_field("ok", true) + ","
		+ json_string_field("assistant", snapshot.last_assistant_message) + ","
		+ json_string_field("representation", snapshot.representation_text) + ","
		+ json_string_field("model", snapshot.model) + ","
		+ json_string_field("status", snapshot.status_text)
		+ "}";
	return result;
}

} // namespace droidcli::net
