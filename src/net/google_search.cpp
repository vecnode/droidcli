#include "net/google_search.hpp"

#include "net/json.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace metaagent::net {
namespace {

core::String url_encode(const core::String& value)
{
	static const char* kHexDigits = "0123456789ABCDEF";
	core::String encoded;
	encoded.reserve(value.size() * 3);
	for (const unsigned char character : value)
	{
		if (std::isalnum(character) || character == '-' || character == '_'
			|| character == '.' || character == '~')
		{
			encoded.push_back(static_cast<char>(character));
		}
		else if (character == ' ')
		{
			encoded += "%20";
		}
		else
		{
			encoded.push_back('%');
			encoded.push_back(kHexDigits[(character >> 4) & 0xF]);
			encoded.push_back(kHexDigits[character & 0xF]);
		}
	}
	return encoded;
}

// Splits the body of a JSON array (the text between '[' and ']', exclusive)
// into its top-level '{...}' object substrings, tracking brace depth and
// string literals (so braces inside quoted strings don't throw off the
// count). No JSON library - consistent with the rest of net/json.hpp.
core::Array<core::String> split_top_level_objects(const core::String& array_body)
{
	core::Array<core::String> objects;
	int depth = 0;
	bool in_string = false;
	size_t object_start = core::String::npos;

	for (size_t index = 0; index < array_body.size(); ++index)
	{
		const char character = array_body[index];

		if (in_string)
		{
			if (character == '\\' && index + 1 < array_body.size())
			{
				++index; // skip the escaped character
			}
			else if (character == '"')
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

		if (character == '{')
		{
			if (depth == 0)
			{
				object_start = index;
			}
			++depth;
		}
		else if (character == '}')
		{
			--depth;
			if (depth == 0 && object_start != core::String::npos)
			{
				objects.push_back(array_body.substr(object_start, index - object_start + 1));
				object_start = core::String::npos;
			}
		}
	}

	return objects;
}

// Finds the "items" array in the response and returns the text strictly
// between its enclosing '[' and matching ']'.
bool find_items_array_body(const core::String& json, core::String& out_body)
{
	const size_t key_pos = json.find("\"items\"");
	if (key_pos == core::String::npos)
	{
		return false;
	}

	const size_t open_bracket = json.find('[', key_pos);
	if (open_bracket == core::String::npos)
	{
		return false;
	}

	int depth = 0;
	bool in_string = false;
	for (size_t index = open_bracket; index < json.size(); ++index)
	{
		const char character = json[index];

		if (in_string)
		{
			if (character == '\\' && index + 1 < json.size())
			{
				++index;
			}
			else if (character == '"')
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

		if (character == '[')
		{
			++depth;
		}
		else if (character == ']')
		{
			--depth;
			if (depth == 0)
			{
				out_body = json.substr(open_bracket + 1, index - open_bracket - 1);
				return true;
			}
		}
	}

	return false;
}

} // namespace

core::String build_google_search_url(const GoogleSearchConfig& config)
{
	std::ostringstream url;
	url << "https://www.googleapis.com/customsearch/v1"
		<< "?key=" << url_encode(config.api_key)
		<< "&cx=" << url_encode(config.search_engine_id)
		<< "&q=" << url_encode(config.query);
	if (config.result_count > 0)
	{
		url << "&num=" << std::min(config.result_count, 10);
	}
	return url.str();
}

GoogleSearchResponse parse_google_search_response(
	const int32_t status_code,
	const core::String& response_body)
{
	GoogleSearchResponse response;

	if (status_code < 200 || status_code >= 300)
	{
		response.success = false;
		const core::String message = extract_json_string_field(response_body, "message");
		response.error_message = message.empty()
			? ("Google search request failed (HTTP " + std::to_string(status_code) + ")")
			: message;
		return response;
	}

	response.total_results = extract_json_string_field(response_body, "totalResults");

	core::String items_body;
	if (find_items_array_body(response_body, items_body))
	{
		for (const core::String& object_body : split_top_level_objects(items_body))
		{
			GoogleSearchResultItem item;
			item.title = extract_json_string_field(object_body, "title");
			item.link = extract_json_string_field(object_body, "link");
			item.snippet = extract_json_string_field(object_body, "snippet");
			if (!item.title.empty() || !item.link.empty())
			{
				response.items.push_back(item);
			}
		}
	}

	response.success = true;
	return response;
}

} // namespace metaagent::net
