#include "runtime/host_interfaces.hpp"

namespace droidcli::runtime {

RecordingSnapshot default_recording_snapshot()
{
	RecordingSnapshot snapshot;
	snapshot.status_text = "Recording host not bound.";
	return snapshot;
}

AiSnapshot default_ai_snapshot()
{
	AiSnapshot snapshot;
	snapshot.status_text = "AI host not bound.";
	return snapshot;
}

bool invoke_toggle_recording(const HostServiceCallbacks& callbacks)
{
	if (callbacks.toggle_recording)
	{
		return callbacks.toggle_recording();
	}
	return false;
}

bool invoke_toggle_autopilot(const HostServiceCallbacks& callbacks)
{
	if (callbacks.toggle_autopilot)
	{
		return callbacks.toggle_autopilot();
	}
	return false;
}

RecordingSnapshot invoke_query_recording(const HostServiceCallbacks& callbacks)
{
	if (callbacks.query_recording)
	{
		return callbacks.query_recording();
	}
	return default_recording_snapshot();
}

AiSnapshot invoke_query_ai(const HostServiceCallbacks& callbacks)
{
	if (callbacks.query_ai)
	{
		return callbacks.query_ai();
	}
	return default_ai_snapshot();
}

} // namespace droidcli::runtime
