#include "net/connector.hpp"

#include "net/json.hpp"

#include <sstream>

namespace droidcli::net {

void ConnectorRegistry::register_connector(const Connector& connector)
{
	if (connector.id.empty() || connector.kind.empty())
	{
		return;
	}

	for (Connector& existing : connectors_)
	{
		if (existing.id == connector.id)
		{
			existing = connector;
			return;
		}
	}

	connectors_.push_back(connector);
}

bool ConnectorRegistry::unregister_connector(const core::String& connector_id)
{
	for (auto iterator = connectors_.begin(); iterator != connectors_.end(); ++iterator)
	{
		if (iterator->id == connector_id)
		{
			connectors_.erase(iterator);
			return true;
		}
	}
	return false;
}

core::Array<Connector> ConnectorRegistry::list_connectors() const
{
	return connectors_;
}

const Connector* ConnectorRegistry::find_connector(const core::String& connector_id) const
{
	for (const Connector& connector : connectors_)
	{
		if (connector.id == connector_id)
		{
			return &connector;
		}
	}
	return nullptr;
}

core::String build_connectors_json(const core::Array<Connector>& connectors)
{
	std::ostringstream stream;
	// "ok" first - this is also the list_connectors agent tool's result
	// (see AGENTS.md's hard rule); a listing can't really fail, but the
	// model's fabrication-guard scan (a_tool_call_already_succeeded_this_turn,
	// cli/host.cpp) checks every action's result_json for this field, so its
	// absence here previously made a legitimate, already-executed tool call
	// invisible to that check.
	stream << json_bool_field("ok", true) << ",\"connectors\":[";
	for (size_t index = 0; index < connectors.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const Connector& connector = connectors[index];
		stream << '{';
		stream << json_string_field("id", connector.id) << ',';
		stream << json_string_field("kind", connector.kind) << ',';
		stream << json_string_field("base_url", connector.base_url) << ',';
		stream << json_string_field("launch_cmd", connector.launch_cmd) << ',';
		stream << json_string_field("work_dir", connector.work_dir) << ',';
		stream << json_bool_field("enabled", connector.enabled) << ',';
		stream << "\"capabilities\":[";
		for (size_t capability_index = 0; capability_index < connector.capabilities.size();
			++capability_index)
		{
			if (capability_index > 0)
			{
				stream << ',';
			}
			stream << '"' << escape_json_string(connector.capabilities[capability_index]) << '"';
		}
		stream << "]}";
	}
	stream << "]}";
	return stream.str();
}

bool parse_connector_from_json(
	const core::String& json,
	Connector& out_connector,
	core::String& out_error)
{
	out_connector = Connector {};
	out_error.clear();

	out_connector.id = extract_json_string_field(json, "id");
	out_connector.kind = extract_json_string_field(json, "kind");
	if (out_connector.id.empty() || out_connector.kind.empty())
	{
		out_error = "Connector requires id and kind.";
		return false;
	}

	if (out_connector.kind != "http_peer" && out_connector.kind != "launched_process")
	{
		out_error = "Connector kind must be http_peer or launched_process.";
		return false;
	}

	out_connector.base_url = extract_json_string_field(json, "base_url");
	out_connector.launch_cmd = extract_json_string_field(json, "launch_cmd");
	out_connector.work_dir = extract_json_string_field(json, "work_dir");

	bool enabled = true;
	if (extract_json_bool_field(json, "enabled", enabled))
	{
		out_connector.enabled = enabled;
	}

	const core::String capability = extract_json_string_field(json, "capabilities");
	if (!capability.empty())
	{
		out_connector.capabilities.push_back(capability);
	}

	return true;
}

} // namespace droidcli::net
