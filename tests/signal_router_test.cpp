#include "core/types.hpp"
#include "net/signal_router.hpp"
#include "net/signal_types.hpp"

#include <cassert>
#include <iostream>

namespace {

bool g_transport_called = false;

bool test_transport(
	const droidcli::core::String& url,
	const droidcli::core::String& body,
	int32_t& status_code_out,
	droidcli::core::String& response_body_out)
{
	g_transport_called = true;
	assert(url == "http://127.0.0.1:9001/signal");
	assert(body.find("\"type\":\"trigger\"") != droidcli::core::String::npos);
	status_code_out = 200;
	response_body_out = "{\"ok\":true}";
	return true;
}

} // namespace

int main()
{
	using namespace droidcli::net;

	SignalRouter router;
	SignalTarget target;
	target.id = "media-player-cpp";
	target.control_url = "http://127.0.0.1:9001/signal";
	target.capabilities = {"fbo", "tile"};
	router.register_target(target);

	assert(router.list_targets().size() == 1);
	assert(router.find_target("media-player-cpp") != nullptr);

	SignalEnvelope envelope;
	envelope.type = "trigger";
	envelope.target = "media-player-cpp";
	envelope.id = "test-1";
	envelope.payload_json = "{\"name\":\"play\"}";

	const SignalDispatchResult result = router.dispatch(envelope, test_transport);

	assert(g_transport_called);
	assert(result.success);
	assert(result.status_code == 200);

	SignalLogEntry entry;
	entry.direction = "outbound";
	entry.type = "trigger";
	router.append_log(entry);
	assert(router.log_entries().size() == 1);

	const droidcli::core::String envelope_json = build_signal_envelope_json(envelope);
	assert(envelope_json.find("\"payload\":") != droidcli::core::String::npos);

	std::cout << "signal_router_test passed" << std::endl;
	return 0;
}
