#pragma once

#include "droidcli_core.h"

#include <map>
#include <mutex>

namespace droidcli::cli {

// droidcli-infra (ARCHITECTURE.md's Modules diagram) - the OS-execution
// boundary droidcli-runtime launches/stops processes through and gets
// PID/liveness back from.
//
// Tracks external processes launched for launched_process connectors. Each
// process is keyed by a logical id (the connector id) so the host can launch,
// query (PID + running state), and stop it. Centralised so droidcli always
// knows the PID of every process it controls.
struct ProcessInfo {
	core::String key;
	core::String label;
	core::String command;
	core::String working_dir;
	int64_t pid = 0;
	bool running = false;
	core::String status_text = "idle";
};

class ProcessManager {
public:
	~ProcessManager();

	// Launch `command` in `working_dir` under `key`, replacing (stopping) any prior
	// process registered under the same key. Returns false + sets `error_out` on failure.
	bool launch(
		const core::String& key,
		const core::String& label,
		const core::String& working_dir,
		const core::String& command,
		core::String& error_out);

	bool stop(const core::String& key, core::String& error_out);

	core::Array<ProcessInfo> snapshot();

private:
	struct Entry {
		ProcessInfo info;
#ifdef _WIN32
		void* job_handle = nullptr;
		void* process_handle = nullptr;
#else
		int pgid = 0;
#endif
	};

	void stop_locked(Entry& entry);
	void refresh_locked(Entry& entry);

	std::map<core::String, Entry> entries_;
	std::mutex mutex_;
};

} // namespace droidcli::cli
