#include "net/platform_client.hpp"

#include "net/json.hpp"

namespace metaagent::net {
namespace {

core::String trim_trailing_slash(core::String value)
{
	while (!value.empty() && value.back() == '/')
	{
		value.pop_back();
	}
	return value;
}

core::String ensure_leading_slash(core::String value)
{
	if (value.empty())
	{
		return "/";
	}
	if (value.front() != '/')
	{
		value.insert(value.begin(), '/');
	}
	return value;
}

core::String to_upper_ascii(core::String value)
{
	for (char& character : value)
	{
		if (character >= 'a' && character <= 'z')
		{
			character = static_cast<char>(character - ('a' - 'A'));
		}
	}
	return value;
}

} // namespace

core::String build_platform_url(const PlatformEndpointConfig& config)
{
	if (config.base_url.empty())
	{
		return {};
	}

	return trim_trailing_slash(config.base_url) + ensure_leading_slash(config.event_endpoint);
}

PlatformOutboundRequest build_platform_outbound_request(
	const PlatformEndpointConfig& config,
	const PlatformEvent& event)
{
	PlatformOutboundRequest request;
	if (!config.enabled)
	{
		request.error_message = "Platform forwarding disabled.";
		return request;
	}

	request.url = build_platform_url(config);
	if (request.url.empty())
	{
		request.error_message = "Platform forwarding URL is empty.";
		return request;
	}

	const core::String session_id = config.session_id.empty() ? "default" : config.session_id;
	const core::String source = event.source.empty() ? "unreal" : event.source;
	const core::String metadata_body =
		json_string_field("map", event.metadata.map_name) + ","
		+ json_string_field("build", event.metadata.build_label);

	request.body = "{"
		+ json_string_field("source", source) + ","
		+ json_string_field("event", event.event_name) + ","
		+ json_string_field("message", event.message) + ","
		+ json_string_field("session_id", session_id) + ","
		+ json_string_field("timestamp_utc", event.timestamp_utc) + ","
		+ "\"metadata\":{" + metadata_body + "}"
		+ "}";

	request.valid = true;
	return request;
}

PlatformEventResponse parse_platform_event_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok)
{
	PlatformEventResponse result;
	result.transport_ok = transport_ok;
	result.status_code = status_code;

	if (!transport_ok)
	{
		result.error_message = "Network failure while sending event.";
		return result;
	}

	if (status_code < 200 || status_code >= 300)
	{
		result.error_message = response_body.empty()
			? "Platform event returned a non-success HTTP status."
			: response_body;
		return result;
	}

	result.http_success = true;

	bool agent_running = false;
	result.has_agent_running = extract_json_bool_field(response_body, "agent_running", agent_running);
	if (result.has_agent_running)
	{
		result.agent_running = agent_running;
	}

	result.agent_action = extract_json_string_field(response_body, "agent_action");

	if (!result.agent_action.empty())
	{
		result.user_message = "Agent " + to_upper_ascii(result.agent_action) + " ("
			+ (result.agent_running ? "RUNNING" : "STOPPED") + ")";
	}
	else if (result.has_agent_running)
	{
		result.user_message = core::String("Agent ")
			+ (result.agent_running ? "RUNNING" : "STOPPED");
	}

	return result;
}

} // namespace metaagent::net
