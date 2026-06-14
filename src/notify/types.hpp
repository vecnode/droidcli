#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::notify {

struct NotifyMessage {
	core::String text;
	core::String raw_body;
	bool parsed_from_json = false;
};

struct NotifyParseResult {
	bool success = false;
	NotifyMessage message;
};

} // namespace metaagent::notify
