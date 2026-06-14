#include "net/json.hpp"

namespace metaagent::net {

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

} // namespace metaagent::net
