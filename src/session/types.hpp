#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::session {

struct FeatureFlags {
	bool input = true;
	bool camera = true;
	bool ai = true;
	bool networking = true;
	bool recording = true;
	bool ui = true;
	bool particle = true;
};

struct RuntimeSession {
	bool active = true;
	FeatureFlags features;
	core::String map_name = "unknown";
	core::String build_label = "unknown";
	int32_t http_port = 30080;
	bool http_enabled = true;
	bool http_router_bound = false;
};

} // namespace metaagent::session
