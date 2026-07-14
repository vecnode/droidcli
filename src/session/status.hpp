#pragma once

#include "export.hpp"
#include "session/types.hpp"

namespace droidcli::session {

DROIDCLI_API core::String build_http_server_status_text(const RuntimeSession& session);

DROIDCLI_API core::String build_startup_feature_flags_text(const RuntimeSession& session);

} // namespace droidcli::session
