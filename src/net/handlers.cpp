#include "net/handlers.hpp"

#include "net/json.hpp"
#include "notify/parse.hpp"

namespace metaagent::net {
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

} // namespace metaagent::net
