#pragma once

#include "notify/types.hpp"
#include "export.hpp"

namespace droidcli::notify {

DROIDCLI_API NotifyParseResult parse_notify_body(const core::String& body);

} // namespace droidcli::notify
