#pragma once

#include "metaagent.h"

#include <map>
#include <mutex>

namespace metaagent::cli {

// Tracks external processes launched by the desktop host (media-player build/run,
// adapter inference server). Each process is keyed by a logical id so the host can
// launch, query (PID + running state), and stop it. Centralised so the UI always
// knows the PID of every app metaagent controls.
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

} // namespace metaagent::cli
