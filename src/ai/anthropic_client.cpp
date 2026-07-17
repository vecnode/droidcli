#include "ai/anthropic_client.hpp"

#include "net/json.hpp"

namespace droidcli::ai {
namespace {

core::String anthropic_trim_trailing_slash(core::String value)
{
	while (!value.empty() && value.back() == '/')
	{
		value.pop_back();
	}
	return value;
}

// Extracts the raw JSON text of a field's value (object, array, string,
// number, bool, or null) rather than assuming it's a quoted string - needed
// for a tool_use block's "input", which Anthropic sends as a JSON object.
// Named uniquely (anthropic_ prefix) because ollama_client.cpp's own
// anonymous-namespace helper of the same shape lives in the same
// droidcli_core.cpp translation unit - see ARCHITECTURE.md's "Second
// ModelProvider" phase for why this can't just reuse that one.
core::String anthropic_extract_json_raw_value(const core::String& json, const core::String& field_name, const size_t search_from = 0)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle, search_from);
	if (field_index == core::String::npos)
	{
		return {};
	}

	size_t cursor = field_index + needle.size();
	while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'
		|| json[cursor] == '\n' || json[cursor] == '\r'))
	{
		++cursor;
	}
	if (cursor >= json.size())
	{
		return {};
	}

	const char start_char = json[cursor];
	if (start_char == '{' || start_char == '[')
	{
		const char open = start_char;
		const char close = (open == '{') ? '}' : ']';
		size_t depth = 0;
		bool in_string = false;
		const size_t start = cursor;
		for (; cursor < json.size(); ++cursor)
		{
			const char character = json[cursor];
			if (in_string)
			{
				if (character == '\\')
				{
					++cursor;
					continue;
				}
				if (character == '"')
				{
					in_string = false;
				}
				continue;
			}
			if (character == '"')
			{
				in_string = true;
				continue;
			}
			if (character == open)
			{
				++depth;
			}
			else if (character == close)
			{
				--depth;
				if (depth == 0)
				{
					++cursor;
					break;
				}
			}
		}
		return json.substr(start, cursor - start);
	}

	size_t end = cursor;
	while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']')
	{
		++end;
	}
	return json.substr(cursor, end - cursor);
}

// Splits a JSON array (cursor at the opening '[') into its top-level object
// elements - same brace-depth-walk convention as ollama_client.cpp's
// extract_json_object_array_at, renamed to avoid the same-TU collision noted
// above.
core::Array<core::String> anthropic_extract_json_object_array_at(const core::String& json, size_t cursor)
{
	core::Array<core::String> objects;
	if (cursor >= json.size() || json[cursor] != '[')
	{
		return objects;
	}
	++cursor;

	while (cursor < json.size())
	{
		while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'
			|| json[cursor] == '\n' || json[cursor] == '\r' || json[cursor] == ','))
		{
			++cursor;
		}
		if (cursor >= json.size() || json[cursor] == ']')
		{
			break;
		}
		if (json[cursor] != '{')
		{
			break;
		}

		size_t depth = 0;
		const size_t start = cursor;
		for (; cursor < json.size(); ++cursor)
		{
			if (json[cursor] == '{')
			{
				++depth;
			}
			else if (json[cursor] == '}')
			{
				--depth;
				if (depth == 0)
				{
					++cursor;
					break;
				}
			}
		}
		objects.push_back(json.substr(start, cursor - start));
	}

	return objects;
}

// Anthropic's Messages API requires strict user/assistant role alternation
// and non-empty content per message - droidcli's internal transcript is
// finer-grained (a hop with two tool calls records two consecutive Tool-role
// ChatMessages with no Assistant message between them). Rather than storing
// Anthropic-specific structure in ChatMessage, this maps System -> the
// top-level "system" field, User/Tool -> "user", Assistant -> "assistant",
// and merges consecutive same-mapped-role entries into one message so the
// result is always strictly alternating regardless of droidcli's own
// transcript granularity. A Tool-role message's content is prefixed
// ("Tool result: ...") so the model can tell it apart from a real user
// utterance despite both mapping to the "user" role.
core::String anthropic_serialize_messages(const core::Array<ChatMessage>& messages, core::String& system_out)
{
	struct Turn
	{
		bool is_user = true;
		core::String text;
	};
	core::Array<Turn> turns;
	system_out.clear();

	for (const ChatMessage& message : messages)
	{
		if (message.role == ChatRole::System)
		{
			if (!system_out.empty())
			{
				system_out += "\n\n";
			}
			system_out += message.content;
			continue;
		}

		const bool is_user = (message.role == ChatRole::User || message.role == ChatRole::Tool);
		const core::String text = (message.role == ChatRole::Tool)
			? ("Tool result: " + message.content)
			: message.content;

		if (!turns.empty() && turns.back().is_user == is_user)
		{
			turns.back().text += "\n\n" + text;
		}
		else
		{
			turns.push_back(Turn{is_user, text});
		}
	}

	core::String body = "[";
	for (size_t index = 0; index < turns.size(); ++index)
	{
		if (index > 0)
		{
			body += ",";
		}
		// Anthropic rejects empty message content - an Assistant turn that
		// only made a tool call (no narration text) would otherwise
		// serialize as "".
		const core::String text = turns[index].text.empty()
			? core::String("(used a tool)")
			: turns[index].text;
		body += "{"
			+ droidcli::net::json_string_field("role", turns[index].is_user ? "user" : "assistant") + ","
			+ droidcli::net::json_string_field("content", text)
			+ "}";
	}
	body += "]";
	return body;
}

core::String anthropic_serialize_tools(const core::Array<ToolDefinition>& tools)
{
	core::String body = "[";
	for (size_t index = 0; index < tools.size(); ++index)
	{
		if (index > 0)
		{
			body += ",";
		}
		const ToolDefinition& tool = tools[index];
		const core::String schema = tool.parameters_json_schema.empty()
			? core::String("{}")
			: tool.parameters_json_schema;
		body += "{"
			+ droidcli::net::json_string_field("name", tool.name) + ","
			+ droidcli::net::json_string_field("description", tool.description) + ","
			+ "\"input_schema\":" + schema
			+ "}";
	}
	body += "]";
	return body;
}

} // namespace

core::String build_anthropic_messages_url(const AnthropicConfig& config)
{
	if (config.base_url.empty())
	{
		return {};
	}
	return anthropic_trim_trailing_slash(config.base_url) + "/v1/messages";
}

AnthropicOutboundRequest build_anthropic_messages_request(
	const AnthropicConfig& config,
	const core::Array<ChatMessage>& messages,
	const core::Array<ToolDefinition>& tools)
{
	AnthropicOutboundRequest request;
	if (!config.enabled)
	{
		request.error_message = "Anthropic integration is disabled.";
		return request;
	}
	if (config.api_key.empty())
	{
		request.error_message = "Anthropic API key is empty.";
		return request;
	}
	if (config.model.empty())
	{
		request.error_message = "Anthropic model name is empty.";
		return request;
	}
	if (messages.empty())
	{
		request.error_message = "Chat transcript is empty.";
		return request;
	}

	request.url = build_anthropic_messages_url(config);
	if (request.url.empty())
	{
		request.error_message = "Anthropic base URL is empty.";
		return request;
	}

	core::String system_prompt;
	const core::String messages_json = anthropic_serialize_messages(messages, system_prompt);

	request.body = "{"
		+ droidcli::net::json_string_field("model", config.model) + ","
		+ "\"max_tokens\":" + std::to_string(config.max_tokens) + ","
		+ "\"messages\":" + messages_json;
	if (!system_prompt.empty())
	{
		request.body += "," + droidcli::net::json_string_field("system", system_prompt);
	}
	if (!tools.empty())
	{
		request.body += ",\"tools\":" + anthropic_serialize_tools(tools);
	}
	request.body += "}";

	request.headers.push_back("x-api-key: " + config.api_key);
	request.headers.push_back("anthropic-version: " + config.api_version);

	request.valid = true;
	return request;
}

AnthropicChatResponse parse_anthropic_messages_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok)
{
	AnthropicChatResponse result;
	result.transport_ok = transport_ok;
	result.status_code = status_code;

	if (!transport_ok)
	{
		result.error_message = "Network failure while calling Anthropic.";
		return result;
	}

	if (status_code < 200 || status_code >= 300)
	{
		// Anthropic's error body is {"type":"error","error":{"type":"...",
		// "message":"..."}} - surface the human-readable message when
		// present rather than the raw envelope.
		const core::String api_error_message = droidcli::net::extract_json_string_field(response_body, "message");
		result.error_message = !api_error_message.empty()
			? api_error_message
			: (response_body.empty() ? "Anthropic returned a non-success HTTP status." : response_body);
		return result;
	}

	result.http_success = true;
	result.model = droidcli::net::extract_json_string_field(response_body, "model");
	result.stop_reason = droidcli::net::extract_json_string_field(response_body, "stop_reason");

	int64_t numeric_field = 0;
	if (droidcli::net::extract_json_int_field(response_body, "input_tokens", numeric_field))
	{
		result.input_tokens = numeric_field;
	}
	if (droidcli::net::extract_json_int_field(response_body, "output_tokens", numeric_field))
	{
		result.output_tokens = numeric_field;
	}

	const size_t content_key = response_body.find("\"content\":");
	if (content_key != core::String::npos)
	{
		const size_t bracket = response_body.find('[', content_key);
		if (bracket != core::String::npos)
		{
			for (const core::String& block : anthropic_extract_json_object_array_at(response_body, bracket))
			{
				const core::String block_type = droidcli::net::extract_json_string_field(block, "type");
				if (block_type == "text")
				{
					if (!result.assistant_message.empty())
					{
						result.assistant_message += "\n";
					}
					result.assistant_message += droidcli::net::extract_json_string_field(block, "text");
				}
				else if (block_type == "tool_use")
				{
					ToolCall call;
					call.id = droidcli::net::extract_json_string_field(block, "id");
					call.name = droidcli::net::extract_json_string_field(block, "name");
					call.arguments_json = anthropic_extract_json_raw_value(block, "input");
					if (!call.name.empty())
					{
						result.tool_calls.push_back(call);
					}
				}
			}
		}
	}

	if (result.assistant_message.empty() && result.tool_calls.empty())
	{
		result.error_message = "Anthropic response did not contain assistant text or tool calls.";
	}

	return result;
}

} // namespace droidcli::ai
