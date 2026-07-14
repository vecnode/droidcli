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
	const core::Array<ChatMessage>& messages)
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

	if (config.temperature >= 0.0f)
	{
		request.body += ",\"options\":{\"temperature\":" + std::to_string(config.temperature) + "}";
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

	bool done = false;
	if (extract_json_bool_field(response_body, "done", done))
	{
		result.done = done;
	}
	else
	{
		result.done = !result.assistant_message.empty();
	}

	if (result.assistant_message.empty())
	{
		result.error_message = "Ollama response did not contain assistant text.";
	}

	return result;
}

} // namespace droidcli::ai
