#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <cstdint>
#include <optional>

namespace droidcli::app {

struct Task {
	core::String id;
	core::String connector_id;
	core::String command;
	core::String payload_json;
	core::String status; // "pending" | "running" | "done" | "failed"
	int64_t created_at_ms = 0;
	int64_t updated_at_ms = 0;
	// 0 means "runnable as soon as claimed" (the pre-scheduler default).
	// Otherwise an absolute epoch-ms deadline computed at enqueue() time from
	// the request's "delay_ms" field - claim_next() will not return this task
	// until the wall clock passes it, letting a caller queue "run this in N
	// minutes" work without a separate scheduler subsystem.
	int64_t scheduled_for_ms = 0;
	core::String error_message;
	// Result payload for commands that produce more than a bare success flag,
	// e.g. "run" (captured stdout/stderr/exit_code as JSON). Empty for tasks
	// whose result is just their terminal status.
	core::String result_json;
};

class TaskQueue {
public:
	core::String enqueue(Task task);
	std::optional<Task> claim_next();
	bool complete(const core::String& task_id, const core::String& result_json = {});
	bool fail(const core::String& task_id, const core::String& error);
	std::optional<Task> find(const core::String& task_id) const;
	core::Array<Task> list() const;

private:
	core::Array<Task> tasks_;
	static constexpr size_t kMaxHistoryEntries = 256;

	void trim_history();
};

DROIDCLI_API core::String build_task_json(const Task& task);

DROIDCLI_API core::String build_tasks_json(const core::Array<Task>& tasks);

// Parses "connector_id"/"command"/"payload_json" plus an optional
// "delay_ms" (milliseconds from now before the task becomes claimable),
// resolved here into an absolute Task::scheduled_for_ms deadline (same wall
// clock read as enqueue()'s created_at_ms, just taken at parse time).
DROIDCLI_API bool parse_task_request_from_json(
	const core::String& json,
	Task& out_task,
	core::String& out_error);

} // namespace droidcli::app
