#pragma once

#include "core/types.hpp"

namespace droidcli::tools {

/** Synchronous HTTP POST with JSON body. Returns false on socket/transport failure. */
bool sync_http_post_json(
	const core::String& url,
	const core::String& body,
	int32_t& status_code_out,
	core::String& response_body_out);

/** Synchronous HTTP GET. Returns false on socket/transport failure. */
bool sync_http_get(
	const core::String& url,
	int32_t& status_code_out,
	core::String& response_body_out);

} // namespace droidcli::tools
