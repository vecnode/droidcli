#include "core/types.hpp"
#include "net/connector.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::net;

	ConnectorRegistry registry;
	Connector connector;
	connector.id = "adapter-example";
	connector.kind = "http_peer";
	connector.base_url = "http://127.0.0.1:8008";
	connector.capabilities = {"summarize"};
	registry.register_connector(connector);

	assert(registry.list_connectors().size() == 1);
	assert(registry.find_connector("adapter-example") != nullptr);
	assert(registry.find_connector("adapter-example")->base_url == "http://127.0.0.1:8008");

	Connector second;
	second.id = "media-player-example";
	second.kind = "launched_process";
	second.launch_cmd = "media-player-cpp.exe";
	second.work_dir = "C:/media-player";
	registry.register_connector(second);
	assert(registry.list_connectors().size() == 2);

	assert(registry.unregister_connector("adapter-example"));
	assert(registry.list_connectors().size() == 1);
	assert(registry.find_connector("adapter-example") == nullptr);
	assert(!registry.unregister_connector("adapter-example"));

	const droidcli::core::String json = build_connectors_json(registry.list_connectors());
	assert(json.find("media-player-example") != droidcli::core::String::npos);
	assert(json.find("launched_process") != droidcli::core::String::npos);

	Connector parsed;
	droidcli::core::String error;
	const droidcli::core::String single_json =
		"{\"id\":\"adapter-example\",\"kind\":\"http_peer\",\"base_url\":\"http://127.0.0.1:8008\","
		"\"enabled\":true,\"capabilities\":\"summarize\"}";
	assert(parse_connector_from_json(single_json, parsed, error));
	assert(error.empty());
	assert(parsed.id == "adapter-example");
	assert(parsed.kind == "http_peer");
	assert(parsed.base_url == "http://127.0.0.1:8008");
	assert(parsed.enabled);

	Connector round_trip_source;
	round_trip_source.id = "roundtrip";
	round_trip_source.kind = "launched_process";
	round_trip_source.launch_cmd = "run.exe";
	round_trip_source.work_dir = "C:/work";
	round_trip_source.enabled = false;
	droidcli::core::Array<Connector> single { round_trip_source };
	const droidcli::core::String built = build_connectors_json(single);
	assert(built.find("\"kind\":\"launched_process\"") != droidcli::core::String::npos);
	assert(built.find("\"enabled\":false") != droidcli::core::String::npos);

	Connector invalid;
	droidcli::core::String invalid_error;
	assert(!parse_connector_from_json("{\"id\":\"x\"}", invalid, invalid_error));
	assert(!invalid_error.empty());

	std::cout << "connector_test passed" << std::endl;
	return 0;
}
