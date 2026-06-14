#pragma once

#include "core/types.hpp"

namespace metaagent::tools {

/** Synchronous HTTP POST with JSON body. Returns false on socket/transport failure. */
bool sync_http_post_json(
	const core::String& url,
	const core::String& body,
	int32_t& status_code_out,
	core::String& response_body_out);

} // namespace metaagent::tools
