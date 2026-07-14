#include "droidcli.h"

#include <cassert>

int main()
{
	using namespace droidcli::runtime;

	assert(!default_recording_snapshot().capture_active);
	assert(!default_ai_snapshot().autopilot_enabled);
	assert(!invoke_toggle_recording({}));
	assert(!invoke_toggle_autopilot({}));

	bool toggled = false;
	HostServiceCallbacks callbacks;
	callbacks.toggle_recording = [&toggled]() { toggled = true; return true; };
	assert(invoke_toggle_recording(callbacks));
	assert(toggled);

	HostServiceCallbacks query_callbacks;
	query_callbacks.query_recording = []() {
		RecordingSnapshot snapshot;
		snapshot.capture_active = true;
		snapshot.status_text = "Recording: ON";
		return snapshot;
	};
	query_callbacks.query_ai = []() {
		AiSnapshot snapshot;
		snapshot.autopilot_enabled = true;
		return snapshot;
	};
	assert(invoke_query_recording(query_callbacks).capture_active);
	assert(invoke_query_ai(query_callbacks).autopilot_enabled);

	return 0;
}
