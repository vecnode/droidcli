#include "net/signal_router.hpp"

#include "net/json.hpp"

#include <sstream>

namespace droidcli::net {

void SignalRouter::register_target(const SignalTarget& target)
{
	if (target.id.empty() || target.control_url.empty())
	{
		return;
	}

	for (SignalTarget& existing : targets_)
	{
		if (existing.id == target.id)
		{
			existing = target;
			return;
		}
	}

	targets_.push_back(target);
}

bool SignalRouter::unregister_target(const core::String& target_id)
{
	for (auto iterator = targets_.begin(); iterator != targets_.end(); ++iterator)
	{
		if (iterator->id == target_id)
		{
			targets_.erase(iterator);
			return true;
		}
	}
	return false;
}

core::Array<SignalTarget> SignalRouter::list_targets() const
{
	return targets_;
}

const SignalTarget* SignalRouter::find_target(const core::String& target_id) const
{
	for (const SignalTarget& target : targets_)
	{
		if (target.id == target_id)
		{
			return &target;
		}
	}
	return nullptr;
}

SignalDispatchResult SignalRouter::dispatch(
	const SignalEnvelope& envelope,
	const SignalTransportFn& transport) const
{
	SignalDispatchResult result;
	result.signal_id = envelope.id;

	if (envelope.target.empty())
	{
		result.error_message = "Signal target is required.";
		return result;
	}

	const SignalTarget* target = find_target(envelope.target);
	if (target == nullptr)
	{
		result.error_message = "Unknown target: " + envelope.target;
		return result;
	}

	if (!target->enabled)
	{
		result.error_message = "Target is disabled: " + envelope.target;
		return result;
	}

	result.target_id = target->id;
	const core::String body = build_signal_envelope_json(envelope);

	if (transport == nullptr)
	{
		result.error_message = "Signal transport is not available.";
		return result;
	}

	const bool transport_ok = transport(
		target->control_url,
		body,
		result.status_code,
		result.response_body);

	result.success = transport_ok && result.status_code >= 200 && result.status_code < 300;
	if (!transport_ok)
	{
		result.error_message = "Transport failure while sending signal.";
	}
	else if (!result.success)
	{
		result.error_message = result.response_body.empty()
			? "Target returned a non-success HTTP status."
			: result.response_body;
	}

	return result;
}

void SignalRouter::append_log(const SignalLogEntry& entry)
{
	log_.push_back(entry);
	if (log_.size() > kMaxLogEntries)
	{
		log_.erase(log_.begin());
	}
}

core::Array<SignalLogEntry> SignalRouter::log_entries() const
{
	return log_;
}

void SignalRouter::clear_log()
{
	log_.clear();
}

core::String build_targets_json(const core::Array<SignalTarget>& targets)
{
	std::ostringstream stream;
	stream << "{\"targets\":[";
	for (size_t index = 0; index < targets.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const SignalTarget& target = targets[index];
		stream << '{';
		stream << json_string_field("id", target.id) << ',';
		stream << json_string_field("control_url", target.control_url) << ',';
		stream << json_bool_field("enabled", target.enabled) << ',';
		stream << "\"capabilities\":[";
		for (size_t capability_index = 0; capability_index < target.capabilities.size();
			++capability_index)
		{
			if (capability_index > 0)
			{
				stream << ',';
			}
			stream << '"' << escape_json_string(target.capabilities[capability_index]) << '"';
		}
		stream << "]}";
	}
	stream << "]}";
	return stream.str();
}

core::String build_signal_log_json(const core::Array<SignalLogEntry>& entries)
{
	std::ostringstream stream;
	stream << "{\"entries\":[";
	for (size_t index = 0; index < entries.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const SignalLogEntry& entry = entries[index];
		stream << '{';
		stream << json_string_field("direction", entry.direction) << ',';
		stream << json_string_field("signal_id", entry.signal_id) << ',';
		stream << json_string_field("type", entry.type) << ',';
		stream << json_string_field("target", entry.target) << ',';
		stream << json_string_field("summary", entry.summary) << ',';
		stream << json_bool_field("delivered", entry.delivered) << ',';
		stream << "\"status_code\":" << entry.status_code;
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

bool parse_target_from_json(
	const core::String& json,
	SignalTarget& out_target,
	core::String& out_error)
{
	out_target = SignalTarget {};
	out_error.clear();

	out_target.id = extract_json_string_field(json, "id");
	out_target.control_url = extract_json_string_field(json, "control_url");
	if (out_target.id.empty() || out_target.control_url.empty())
	{
		out_error = "Target requires id and control_url.";
		return false;
	}

	bool enabled = true;
	if (extract_json_bool_field(json, "enabled", enabled))
	{
		out_target.enabled = enabled;
	}

	const core::String capabilities = extract_json_string_field(json, "capabilities");
	if (!capabilities.empty())
	{
		out_target.capabilities.push_back(capabilities);
	}

	return true;
}

} // namespace droidcli::net
