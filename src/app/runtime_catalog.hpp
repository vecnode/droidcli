#pragma once

#include "export.hpp"
#include "session/types.hpp"

namespace metaagent::app {

struct RuntimeDescriptor {
	core::String id;
	core::String title;
	core::String summary;
	core::String host_scope;
	bool active_in_session = false;
};

METAAGENT_API core::Array<RuntimeDescriptor> build_runtime_catalog(
	const session::RuntimeSession& session);

METAAGENT_API core::String build_runtime_catalog_json(
	const session::RuntimeSession& session);

} // namespace metaagent::app
