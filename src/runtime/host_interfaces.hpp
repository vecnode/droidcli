#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <functional>

namespace droidcli::runtime {

struct RecordingSnapshot {
	bool runtime_enabled = false;
	bool capture_active = false;
	core::String last_output_path;
	core::String status_text;
};

struct AiSnapshot {
	bool runtime_enabled = false;
	bool autopilot_enabled = false;
	core::String status_text;
};

struct HostServiceCallbacks {
	std::function<bool()> toggle_recording;
	std::function<bool()> toggle_autopilot;
	std::function<RecordingSnapshot()> query_recording;
	std::function<AiSnapshot()> query_ai;
};

DROIDCLI_API RecordingSnapshot default_recording_snapshot();

DROIDCLI_API AiSnapshot default_ai_snapshot();

DROIDCLI_API bool invoke_toggle_recording(const HostServiceCallbacks& callbacks);

DROIDCLI_API bool invoke_toggle_autopilot(const HostServiceCallbacks& callbacks);

DROIDCLI_API RecordingSnapshot invoke_query_recording(const HostServiceCallbacks& callbacks);

DROIDCLI_API AiSnapshot invoke_query_ai(const HostServiceCallbacks& callbacks);

} // namespace droidcli::runtime
