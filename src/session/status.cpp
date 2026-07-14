#include "session/status.hpp"

namespace droidcli::session {
namespace {

const char* enabled_label(const bool value)
{
	return value ? "On" : "Off";
}

const char* bound_label(const bool value)
{
	return value ? "bound" : "not-bound";
}

const char* http_enabled_label(const bool value)
{
	return value ? "enabled" : "disabled";
}

} // namespace

core::String build_http_server_status_text(const RuntimeSession& session)
{
	return "HTTP "
		+ core::String(http_enabled_label(session.http_enabled))
		+ ", port=" + std::to_string(session.http_port)
		+ ", router=" + bound_label(session.http_router_bound)
		+ ", endpoints=/health,/echo,/notify";
}

core::String build_startup_feature_flags_text(const RuntimeSession& session)
{
	return "AI=" + core::String(enabled_label(session.features.ai))
		+ " Networking=" + enabled_label(session.features.networking)
		+ " Recording=" + enabled_label(session.features.recording)
		+ " UI=" + enabled_label(session.features.ui);
}

} // namespace droidcli::session
