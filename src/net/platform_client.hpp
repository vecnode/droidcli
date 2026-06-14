#pragma once

#include "export.hpp"
#include "core/types.hpp"

namespace metaagent::net {

struct PlatformEndpointConfig {
	core::String base_url;
	core::String event_endpoint;
	core::String session_id;
	bool enabled = true;
};

struct PlatformEventMetadata {
	core::String map_name;
	core::String build_label;
};

struct PlatformEvent {
	core::String source;
	core::String event_name;
	core::String message;
	core::String timestamp_utc;
	PlatformEventMetadata metadata;
};

struct PlatformOutboundRequest {
	bool valid = false;
	core::String url;
	core::String body;
	core::String error_message;
};

struct PlatformEventResponse {
	bool transport_ok = false;
	bool http_success = false;
	int32_t status_code = 0;
	bool has_agent_running = false;
	bool agent_running = false;
	core::String agent_action;
	core::String user_message;
	core::String error_message;
};

METAAGENT_API core::String build_platform_url(const PlatformEndpointConfig& config);

METAAGENT_API PlatformOutboundRequest build_platform_outbound_request(
	const PlatformEndpointConfig& config,
	const PlatformEvent& event);

METAAGENT_API PlatformEventResponse parse_platform_event_response(
	int32_t status_code,
	const core::String& response_body,
	bool transport_ok);

} // namespace metaagent::net
