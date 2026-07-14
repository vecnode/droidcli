#pragma once

#include "export.hpp"
#include "session/types.hpp"

namespace droidcli::app {

struct RuntimeDescriptor {
	core::String id;
	core::String title;
	core::String summary;
	core::String host_scope;
	bool active_in_session = false;
};

DROIDCLI_API core::Array<RuntimeDescriptor> build_runtime_catalog(
	const session::RuntimeSession& session);

DROIDCLI_API core::String build_runtime_catalog_json(
	const session::RuntimeSession& session);

} // namespace droidcli::app
