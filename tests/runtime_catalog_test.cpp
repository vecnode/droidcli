#include "app/runtime_catalog.hpp"
#include "core/types.hpp"
#include "session/types.hpp"

#include <cassert>
#include <iostream>

int main()
{
	droidcli::session::RuntimeSession runtime_session;
	runtime_session.features.networking = true;
	runtime_session.features.ai = true;
	runtime_session.features.recording = true;

	const droidcli::core::Array<droidcli::app::RuntimeDescriptor> catalog =
		droidcli::app::build_runtime_catalog(runtime_session);
	assert(catalog.size() >= 6);

	bool found_networking = false;
	bool found_recording = false;
	for (const droidcli::app::RuntimeDescriptor& descriptor : catalog)
	{
		// Every runtime is host-local now; nothing is scoped to an external engine.
		assert(descriptor.host_scope == "core");
		if (descriptor.id == "networking")
		{
			found_networking = true;
			assert(descriptor.active_in_session);
		}
		if (descriptor.id == "recording")
		{
			found_recording = true;
			assert(descriptor.active_in_session);
		}
	}

	assert(found_networking);
	assert(found_recording);

	droidcli::session::RuntimeSession recording_off = runtime_session;
	recording_off.features.recording = false;
	for (const droidcli::app::RuntimeDescriptor& descriptor :
		droidcli::app::build_runtime_catalog(recording_off))
	{
		if (descriptor.id == "recording")
		{
			assert(!descriptor.active_in_session);
		}
	}

	const droidcli::core::String json =
		droidcli::app::build_runtime_catalog_json(runtime_session);
	assert(json.find("\"runtimes\"") != droidcli::core::String::npos);
	assert(json.find("ue5") == droidcli::core::String::npos);

	std::cout << "runtime_catalog_test passed" << std::endl;
	return 0;
}
