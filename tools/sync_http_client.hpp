#pragma once

#include "core/types.hpp"

namespace droidcli::tools {

/** Synchronous HTTP POST with JSON body. Returns false on socket/transport failure.
 * extra_headers is a list of raw "Name: value" lines (no trailing CRLF) sent
 * in addition to the Content-Type/Content-Length headers this function
 * already adds - e.g. an external OpenAI-compatible backend's
 * "Authorization: Bearer ..." header. Empty by default so every existing
 * caller (the local-Ollama default, connector calls) is unaffected. */
bool sync_http_post_json(
	const core::String& url,
	const core::String& body,
	int32_t& status_code_out,
	core::String& response_body_out,
	const core::Array<core::String>& extra_headers = {});

/** Synchronous HTTP GET. Returns false on socket/transport failure. */
bool sync_http_get(
	const core::String& url,
	int32_t& status_code_out,
	core::String& response_body_out);

} // namespace droidcli::tools
