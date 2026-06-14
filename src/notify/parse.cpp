#include "notify/parse.hpp"

namespace metaagent::notify {
namespace {

core::String trim_ascii(const core::String& input)
{
	size_t start = 0;
	while (start < input.size() && (input[start] == ' ' || input[start] == '\t' || input[start] == '\r' || input[start] == '\n'))
	{
		++start;
	}

	size_t end = input.size();
	while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r' || input[end - 1] == '\n'))
	{
		--end;
	}

	return input.substr(start, end - start);
}

core::String extract_json_string_field(const core::String& json, const core::String& field_name)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle);
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

} // namespace

NotifyParseResult parse_notify_body(const core::String& body)
{
	NotifyParseResult result;
	result.message.raw_body = body;

	const core::String trimmed = trim_ascii(body);
	if (trimmed.empty())
	{
		result.success = true;
		result.message.text = "(no message)";
		return result;
	}

	if (trimmed.front() == '{')
	{
		const core::String message_field = extract_json_string_field(trimmed, "message");
		if (!message_field.empty())
		{
			result.success = true;
			result.message.text = message_field;
			result.message.parsed_from_json = true;
			return result;
		}
	}

	result.success = true;
	result.message.text = trimmed;
	return result;
}

} // namespace metaagent::notify
