#include "net/json.hpp"

namespace droidcli::net {

core::String escape_json_string(const core::String& input)
{
	core::String escaped;
	escaped.reserve(input.size() + 8);
	for (const char character : input)
	{
		switch (character)
		{
		case '\\':
			escaped += "\\\\";
			break;
		case '"':
			escaped += "\\\"";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped += character;
			break;
		}
	}
	return escaped;
}

core::String json_string_field(const core::String& key, const core::String& value)
{
	return "\"" + key + "\":\"" + escape_json_string(value) + "\"";
}

core::String json_bool_field(const core::String& key, const bool value)
{
	return "\"" + key + "\":" + (value ? "true" : "false");
}

core::String extract_json_string_field(
	const core::String& json,
	const core::String& field_name,
	const size_t search_from)
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

bool extract_json_bool_field(
	const core::String& json,
	const core::String& field_name,
	bool& out_value,
	const size_t search_from)
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

bool extract_json_int_field(
	const core::String& json,
	const core::String& field_name,
	int64_t& out_value,
	const size_t search_from)
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

	const size_t start = cursor;
	if (cursor < json.size() && json[cursor] == '-')
	{
		++cursor;
	}
	const size_t digits_start = cursor;
	while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9')
	{
		++cursor;
	}
	if (cursor == digits_start)
	{
		return false;
	}

	out_value = std::stoll(json.substr(start, cursor - start));
	return true;
}

} // namespace droidcli::net
