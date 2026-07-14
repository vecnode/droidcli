#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::session {

struct FeatureFlags {
	bool ai = true;
	bool networking = true;
	bool recording = true;
	bool ui = true;
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

} // namespace droidcli::session
