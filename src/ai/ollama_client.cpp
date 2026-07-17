#include "ai/ollama_client.hpp"

#include "net/json.hpp"

namespace droidcli::ai {
namespace {

core::String trim_trailing_slash(core::String value)
{
	while (!value.empty() && value.back() == '/')
	{
		value.pop_back();
	}
	return value;
}

core::String extract_json_string_field(const core::String& json, const core::String& field_name, const size_t search_from = 0)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle, search_from);
	if (field_index == core::String::npos)
	{
		return {};
	}

	size_t cursor = field_index + needle.size();
	while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
	{
		++cursor;
	}

	if (cursor >= json.size() || json[cursor] != '"')
	{
		return {};
	}

	++cursor;
	core::String value;
	while (cursor < json.size())
	{
		const char character = json[cursor++];
		if (character == '\\' && cursor < json.size())
		{
			const char escaped = json[cursor++];
			switch (escaped)
			{
			case '"':
				value += '"';
				break;
			case '\\':
				value += '\\';
				break;
			case 'n':
				value += '\n';
				break;
			case 'r':
				value += '\r';
				break;
			case 't':
				value += '\t';
				break;
			default:
				value += escaped;
				break;
			}
			continue;
		}

		if (character == '"')
		{
			break;
		}

		value += character;
	}

	return value;
}

bool extract_json_bool_field(const core::String& json, const core::String& field_name, bool& out_value, const size_t search_from = 0)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle, search_from);
	if (field_index == core::String::npos)
	{
		return false;
	}

	size_t cursor = field_index + needle.size();
	while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
	{
		++cursor;
	}

	if (json.compare(cursor, 4, "true") == 0)
	{
		out_value = true;
		return true;
	}
	if (json.compare(cursor, 5, "false") == 0)
	{
		out_value = false;
		return true;
	}

	return false;
}

// Extracts the raw JSON text of a field's value (object, array, string,
// number, bool, or null) rather than assuming it's a quoted string - needed
// for "arguments", which Ollama sends as a JSON object, not a JSON-encoded
// string like OpenAI's tool-calling wire format.
core::String extract_json_raw_value(const core::String& json, const core::String& field_name, const size_t search_from = 0)
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

	if (start_char == '"')
	{
		// Return the raw quoted-and-escaped text, not the decoded string.
		size_t end = cursor + 1;
		while (end < json.size())
		{
			if (json[end] == '\\')
			{
				end += 2;
				continue;
			}
			if (json[end] == '"')
			{
				++end;
				break;
			}
			++end;
		}
		return json.substr(cursor, end - cursor);
	}

	size_t end = cursor;
	while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']')
	{
		++end;
	}
	return json.substr(cursor, end - cursor);
}

// Splits a JSON array (cursor at the opening '[') into its top-level object
// elements, matching the brace-depth-walk convention used elsewhere in this
// codebase (e.g. cli/droidcli.cpp's extract_connector_objects).
core::Array<core::String> extract_json_object_array_at(const core::String& json, size_t cursor)
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

core::String serialize_tools(const core::Array<ToolDefinition>& tools)
{
	core::String body = "[";
	for (size_t index = 0; index < tools.size(); ++index)
	{
		if (index > 0)
		{
			body += ",";
		}
		const ToolDefinition& tool = tools[index];
		const core::String parameters = tool.parameters_json_schema.empty()
			? core::String("{}")
			: tool.parameters_json_schema;
		body += "{\"type\":\"function\",\"function\":{"
			+ droidcli::net::json_string_field("name", tool.name) + ","
			+ droidcli::net::json_string_field("description", tool.description) + ","
			+ "\"parameters\":" + parameters
			+ "}}";
	}
	body += "]";
	return body;
}

core::String serialize_chat_messages(const core::Array<ChatMessage>& messages)
{
	core::String body = "[";
	for (size_t index = 0; index < messages.size(); ++index)
	{
		if (index > 0)
		{
			body += ",";
		}

		const ChatMessage& message = messages[index];
		body += "{"
			+ droidcli::net::json_string_field("role", chat_role_to_string(message.role)) + ","
			+ droidcli::net::json_string_field("content", message.content)
			+ "}";
	}
	body += "]";
	return body;
}

} // namespace

core::String build_ollama_chat_url(const OllamaConfig& config)
{
	if (config.base_url.empty())
	{
		return {};
	}

	return trim_trailing_slash(config.base_url) + "/api/chat";
}

OllamaOutboundRequest build_ollama_chat_request(
	const OllamaConfig& config,
	const core::Array<ChatMessage>& messages,
	const core::Array<ToolDefinition>& tools)
{
	OllamaOutboundRequest request;
	if (!config.enabled)
	{
		request.error_message = "Ollama integration is disabled.";
		return request;
	}

	if (config.model.empty())
	{
		request.error_message = "Ollama model name is empty.";
		return request;
	}

	if (messages.empty())
	{
		request.error_message = "Chat transcript is empty.";
		return request;
	}

	request.url = build_ollama_chat_url(config);
	if (request.url.empty())
	{
		request.error_message = "Ollama base URL is empty.";
		return request;
	}

	request.body = "{"
		+ droidcli::net::json_string_field("model", config.model) + ","
		+ "\"messages\":" + serialize_chat_messages(messages) + ","
		+ droidcli::net::json_bool_field("stream", config.stream);

	// num_ctx is sent on every call, unconditionally (unlike temperature,
	// which is opt-in via a sentinel) - Ollama's own default context window
	// is small enough that a long agent-turn transcript can silently get
	// truncated without this. See "Context window (num_ctx)" in
	// ARCHITECTURE.md's OpenClaude comparison for the incident this closes.
	core::String options_fields = "\"num_ctx\":" + std::to_string(config.num_ctx);
	if (config.temperature >= 0.0f)
	{
		options_fields += ",\"temperature\":" + std::to_string(config.temperature);
	}
	request.body += ",\"options\":{" + options_fields + "}";

	if (!tools.empty())
	{
		request.body += ",\"tools\":" + serialize_tools(tools);
	}

	request.body += "}";
	request.valid = true;
	return request;
}

OllamaChatResponse parse_ollama_chat_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok)
{
	OllamaChatResponse result;
	result.transport_ok = transport_ok;
	result.status_code = status_code;

	if (!transport_ok)
	{
		result.error_message = "Network failure while calling Ollama.";
		return result;
	}

	if (status_code < 200 || status_code >= 300)
	{
		result.error_message = response_body.empty()
			? "Ollama returned a non-success HTTP status."
			: response_body;
		return result;
	}

	result.http_success = true;
	result.model = extract_json_string_field(response_body, "model");

	const size_t message_index = response_body.find("\"message\":");
	if (message_index != core::String::npos)
	{
		result.assistant_message = extract_json_string_field(response_body, "content", message_index);
	}

	if (result.assistant_message.empty())
	{
		result.assistant_message = extract_json_string_field(response_body, "response");
	}

	if (message_index != core::String::npos)
	{
		const size_t tool_calls_key = response_body.find("\"tool_calls\":", message_index);
		if (tool_calls_key != core::String::npos)
		{
			const size_t bracket = response_body.find('[', tool_calls_key);
			if (bracket != core::String::npos)
			{
				for (const core::String& call_object : extract_json_object_array_at(response_body, bracket))
				{
					const size_t function_index = call_object.find("\"function\":");
					const size_t name_search_from = function_index == core::String::npos ? 0 : function_index;

					ToolCall call;
					call.id = extract_json_string_field(call_object, "id");
					call.name = extract_json_string_field(call_object, "name", name_search_from);
					call.arguments_json = extract_json_raw_value(call_object, "arguments", name_search_from);
					if (!call.name.empty())
					{
						result.tool_calls.push_back(call);
					}
				}
			}
		}
	}

	bool done = false;
	if (extract_json_bool_field(response_body, "done", done))
	{
		result.done = done;
	}
	else
	{
		result.done = !result.assistant_message.empty() || !result.tool_calls.empty();
	}

	// Telemetry fields Ollama appends to the final chunk of a chat response -
	// top-level, siblings of "message"/"done", not nested under it. Absent on
	// a non-2xx body (already returned above) or on older Ollama versions;
	// extract_json_int_field leaves the field at its zero default in that
	// case rather than erroring, matching the "done" pattern just above.
	int64_t numeric_field = 0;
	if (droidcli::net::extract_json_int_field(response_body, "total_duration", numeric_field))
	{
		result.total_duration_ns = numeric_field;
	}
	if (droidcli::net::extract_json_int_field(response_body, "eval_duration", numeric_field))
	{
		result.eval_duration_ns = numeric_field;
	}
	if (droidcli::net::extract_json_int_field(response_body, "prompt_eval_count", numeric_field))
	{
		result.prompt_eval_count = numeric_field;
	}
	if (droidcli::net::extract_json_int_field(response_body, "eval_count", numeric_field))
	{
		result.eval_count = numeric_field;
	}
	result.done_reason = extract_json_string_field(response_body, "done_reason");

	if (result.assistant_message.empty() && result.tool_calls.empty())
	{
		result.error_message = "Ollama response did not contain assistant text or tool calls.";
	}

	return result;
}

} // namespace droidcli::ai
