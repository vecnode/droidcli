#include "net/signal_types.hpp"

#include "net/json.hpp"

#include <chrono>

namespace droidcli::net {
namespace {

int64_t current_timestamp_ms()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

core::String extract_json_object_field(const core::String& json, const core::String& field_name)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle);
	if (field_index == core::String::npos)
	{
		return {};
	}

	size_t cursor = field_index + needle.size();
	while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
	{
		++cursor;
	}

	if (cursor >= json.size() || json[cursor] != '{')
	{
		return {};
	}

	size_t depth = 0;
	const size_t start = cursor;
	for (; cursor < json.size(); ++cursor)
	{
		const char character = json[cursor];
		if (character == '{')
		{
			++depth;
		}
		else if (character == '}')
		{
			--depth;
			if (depth == 0)
			{
				return json.substr(start, cursor - start + 1);
			}
		}
	}

	return {};
}

} // namespace

core::String signal_type_display_name(const core::String& type)
{
	if (type == "trigger")
	{
		return "Trigger";
	}
	if (type == "command")
	{
		return "Command";
	}
	if (type == "tile")
	{
		return "Tile";
	}
	if (type == "frame_meta")
	{
		return "Frame Meta";
	}
	if (type == "ai_plan")
	{
		return "AI Plan";
	}
	if (type == "notify")
	{
		return "Notify";
	}
	return type.empty() ? "Unknown" : type;
}

core::String build_signal_envelope_json(const SignalEnvelope& envelope)
{
	const int64_t timestamp_ms =
		envelope.timestamp_ms > 0 ? envelope.timestamp_ms : current_timestamp_ms();

	core::String payload = envelope.payload_json;
	if (payload.empty())
	{
		payload = "{}";
	}
	else if (payload.front() != '{')
	{
		payload = "{" + payload + "}";
	}

	core::String body = "{";
	body += "\"v\":" + std::to_string(envelope.version) + ",";
	body += json_string_field("id", envelope.id) + ",";
	body += json_string_field("type", envelope.type) + ",";
	body += json_string_field("target", envelope.target) + ",";
	body += "\"timestamp_ms\":" + std::to_string(timestamp_ms) + ",";
	body += "\"payload\":" + payload;
	body += "}";
	return body;
}

bool parse_signal_envelope(
	const core::String& json,
	SignalEnvelope& out_envelope,
	core::String& out_error)
{
	out_envelope = SignalEnvelope {};
	out_error.clear();

	const core::String type = extract_json_string_field(json, "type");
	if (type.empty())
	{
		out_error = "Missing signal type.";
		return false;
	}

	out_envelope.type = type;
	out_envelope.id = extract_json_string_field(json, "id");
	out_envelope.target = extract_json_string_field(json, "target");
	out_envelope.payload_json = extract_json_object_field(json, "payload");
	if (out_envelope.payload_json.empty())
	{
		const core::String message = extract_json_string_field(json, "message");
		if (!message.empty())
		{
			out_envelope.payload_json = "{" + json_string_field("message", message) + "}";
		}
		else
		{
			out_envelope.payload_json = "{}";
		}
	}

	return true;
}

} // namespace droidcli::net
