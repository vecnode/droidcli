#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <functional>

namespace droidcli::net {

struct SignalTarget {
	core::String id;
	core::String control_url;
	core::Array<core::String> capabilities;
	bool enabled = true;
};

struct SignalEnvelope {
	int32_t version = 1;
	core::String id;
	core::String type;
	core::String target;
	int64_t timestamp_ms = 0;
	core::String payload_json;
};

struct SignalDispatchResult {
	bool success = false;
	core::String signal_id;
	core::String target_id;
	core::String error_message;
	int32_t status_code = 0;
	core::String response_body;
};

struct SignalLogEntry {
	core::String direction;
	core::String signal_id;
	core::String type;
	core::String target;
	core::String summary;
	bool delivered = false;
	int32_t status_code = 0;
};

using SignalTransportFn = std::function<bool(
	const core::String& url,
	const core::String& body,
	int32_t& status_code_out,
	core::String& response_body_out)>;

DROIDCLI_API core::String signal_type_display_name(const core::String& type);

DROIDCLI_API core::String build_signal_envelope_json(const SignalEnvelope& envelope);

DROIDCLI_API bool parse_signal_envelope(
	const core::String& json,
	SignalEnvelope& out_envelope,
	core::String& out_error);

} // namespace droidcli::net
