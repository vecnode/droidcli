#include "ai/openai_compat_client.hpp"

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

// reasoningKeys are the non-standard Chat Completions message fields under
// which a reasoning-capable model streams (or, here, returns whole) its
// chain-of-thought back alongside "content". DeepSeek/GLM use
// "reasoning_content"; Ollama's own /v1 layer uses "reasoning" (confirmed
// directly against a local glm-4.7-flash response - see ARCHITECTURE.md's
// "The LLM provider"). Checked in order; first match wins.
core::String extract_reasoning_field(const core::String& message_object)
{
	for (const char* key : {"reasoning_content", "reasoning"})
	{
		const core::String value = extract_json_string_field(message_object, key);
		if (!value.empty())
		{
			return value;
		}
	}
	return {};
}

} // namespace

core::String build_openai_chat_url(const OpenAICompatConfig& config)
{
	if (config.base_url.empty())
	{
		return {};
	}

	return trim_trailing_slash(config.base_url) + "/v1/chat/completions";
}

OpenAICompatOutboundRequest build_openai_chat_request(
	const OpenAICompatConfig& config,
	const core::Array<ChatMessage>& messages,
	const core::Array<ToolDefinition>& tools)
{
	OpenAICompatOutboundRequest request;
	if (!config.enabled)
	{
		request.error_message = "The LLM provider is disabled.";
		return request;
	}

	if (config.model.empty())
	{
		request.error_message = "Model name is empty.";
		return request;
	}

	if (messages.empty())
	{
		request.error_message = "Chat transcript is empty.";
		return request;
	}

	request.url = build_openai_chat_url(config);
	if (request.url.empty())
	{
		request.error_message = "Provider base URL is empty.";
		return request;
	}

	request.body = "{"
		+ droidcli::net::json_string_field("model", config.model) + ","
		+ "\"messages\":" + serialize_chat_messages(messages) + ","
		+ droidcli::net::json_bool_field("stream", config.stream);

	if (config.temperature >= 0.0f)
	{
		request.body += ",\"temperature\":" + std::to_string(config.temperature);
	}

	// Ollama's own extension, sent unconditionally on every call (unlike
	// temperature, which is opt-in via a sentinel) - Ollama's own default
	// context window is small enough that a long agent-turn transcript can
	// silently get truncated without this. A sibling top-level field, not
	// nested inside anything the standard OpenAI schema validates, so a
	// strict (non-Ollama) OpenAI-compatible backend simply ignores it. See
	// "Context window (num_ctx)" in ARCHITECTURE.md's OpenClaude comparison
	// for the incident this closes.
	request.body += ",\"options\":{\"num_ctx\":" + std::to_string(config.num_ctx) + "}";

	if (!tools.empty())
	{
		request.body += ",\"tools\":" + serialize_tools(tools);
	}

	request.body += "}";
	request.valid = true;
	return request;
}

OpenAICompatChatResponse parse_openai_chat_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok)
{
	OpenAICompatChatResponse result;
	result.transport_ok = transport_ok;
	result.status_code = status_code;

	if (!transport_ok)
	{
		result.error_message = "Network failure while calling the LLM provider.";
		return result;
	}

	if (status_code < 200 || status_code >= 300)
	{
		result.error_message = response_body.empty()
			? "The LLM provider returned a non-success HTTP status."
			: response_body;
		return result;
	}

	result.http_success = true;
	result.model = extract_json_string_field(response_body, "model");

	// {"choices":[{"index":0,"message":{...},"finish_reason":"..."}], ...}
	// - only the first choice is ever read, matching classify_via_llm's own
	// "take the first, discard the rest" discipline one layer up.
	const size_t choices_key = response_body.find("\"choices\":");
	if (choices_key != core::String::npos)
	{
		const size_t bracket = response_body.find('[', choices_key);
		if (bracket != core::String::npos)
		{
			const core::Array<core::String> choices = extract_json_object_array_at(response_body, bracket);
			if (!choices.empty())
			{
				const core::String& first_choice = choices[0];
				result.assistant_message = extract_json_string_field(first_choice, "content");
				result.done_reason = extract_json_string_field(first_choice, "finish_reason");
				result.thinking_text = extract_reasoning_field(first_choice);

				const size_t tool_calls_key = first_choice.find("\"tool_calls\":");
				if (tool_calls_key != core::String::npos)
				{
					const size_t tool_bracket = first_choice.find('[', tool_calls_key);
					if (tool_bracket != core::String::npos)
					{
						for (const core::String& call_object : extract_json_object_array_at(first_choice, tool_bracket))
						{
							const size_t function_index = call_object.find("\"function\":");
							const size_t name_search_from = function_index == core::String::npos ? 0 : function_index;

							ToolCall call;
							call.id = extract_json_string_field(call_object, "id");
							call.name = extract_json_string_field(call_object, "name", name_search_from);
							// Unlike Ollama's native /api/chat (arguments as a raw
							// JSON object), the OpenAI wire format sends arguments
							// as a JSON-encoded STRING - extract_json_string_field
							// decodes the escaping, handing back the same raw
							// object-JSON text callers already expect in
							// ToolCall::arguments_json.
							call.arguments_json = extract_json_string_field(call_object, "arguments", name_search_from);
							if (!call.name.empty())
							{
								result.tool_calls.push_back(call);
							}
						}
					}
				}
			}
		}
	}

	// A non-streamed response is complete by construction - there is no
	// partial-chunk concept to distinguish, unlike Ollama's native streaming
	// "done" flag.
	result.done = result.http_success;

	int64_t numeric_field = 0;
	if (droidcli::net::extract_json_int_field(response_body, "prompt_tokens", numeric_field))
	{
		result.prompt_tokens = numeric_field;
	}
	if (droidcli::net::extract_json_int_field(response_body, "completion_tokens", numeric_field))
	{
		result.completion_tokens = numeric_field;
	}

	if (result.assistant_message.empty() && result.tool_calls.empty())
	{
		result.error_message = "The LLM provider's response contained no assistant text or tool calls.";
	}

	return result;
}

} // namespace droidcli::ai
