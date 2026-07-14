#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::net {

struct Connector {
	core::String id;
	core::String kind; // "http_peer" or "launched_process"
	core::String base_url; // for http_peer
	core::String launch_cmd; // for launched_process
	core::String work_dir;
	core::Array<core::String> capabilities;
	bool enabled = true;
};

class ConnectorRegistry {
public:
	void register_connector(const Connector& connector);
	bool unregister_connector(const core::String& connector_id);
	core::Array<Connector> list_connectors() const;
	const Connector* find_connector(const core::String& connector_id) const;

private:
	core::Array<Connector> connectors_;
};

METAAGENT_API core::String build_connectors_json(const core::Array<Connector>& connectors);

METAAGENT_API bool parse_connector_from_json(
	const core::String& json,
	Connector& out_connector,
	core::String& out_error);

} // namespace metaagent::net
