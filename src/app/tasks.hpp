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
	// "pending" | "running" | "done" | "failed" | "cancelled" - "done"/
	// "failed" are only ever terminal for a one-shot task (recurrence_ms
	// == 0). A recurring task (recurrence_ms > 0) cycles back to "pending"
	// with a new scheduled_for_ms after each run, whether that run
	// completed or failed - see complete()/fail() below - so its status
	// only reads "done"/"failed" for the instant between a run finishing
	// and tick_tasks() rescheduling it. "cancelled" is the only way to stop
	// a recurring task for good; see cancel().
	core::String status;
	int64_t created_at_ms = 0;
	int64_t updated_at_ms = 0;
	// 0 means "runnable as soon as claimed" (the pre-scheduler default).
	// Otherwise an absolute epoch-ms deadline computed at enqueue() time from
	// the request's "delay_ms" field - claim_next() will not return this task
	// until the wall clock passes it, letting a caller queue "run this in N
	// minutes" work without a separate scheduler subsystem.
	int64_t scheduled_for_ms = 0;
	// 0 (the default) means one-shot: this task runs once and terminates in
	// "done"/"failed". A positive value is a cron/SOP-style recurring
	// interval in milliseconds - after each run, complete()/fail() reset
	// scheduled_for_ms to (now + recurrence_ms) and put the task back to
	// "pending" instead of leaving it terminal, so tick_tasks() picks it up
	// again on its own next due time with no separate scheduler thread.
	int64_t recurrence_ms = 0;
	// How many times claim_next() has returned this task - visible via
	// GET /api/tasks/{id} so a recurring task's run history is observable
	// without a separate log query.
	int64_t run_count = 0;
	core::String error_message;
	// Result payload for commands that produce more than a bare success flag,
	// e.g. "run" (captured stdout/stderr/exit_code as JSON). For a recurring
	// task this is always the *most recent* run's result, overwritten each
	// cycle, not a history of every run.
	core::String result_json;
};

class TaskQueue {
public:
	core::String enqueue(Task task);
	std::optional<Task> claim_next();
	// For a recurring task (recurrence_ms > 0), reschedules back to "pending"
	// with a new scheduled_for_ms instead of leaving it "done" - see Task's
	// status comment above.
	bool complete(const core::String& task_id, const core::String& result_json = {});
	// Same recurrence handling as complete() - a recurring task keeps trying
	// on its next scheduled run instead of dying on one failure, matching
	// cron/SOP semantics; the failure is still recorded in error_message.
	bool fail(const core::String& task_id, const core::String& error);
	// Marks a task "cancelled" so tick_tasks()/claim_next() never picks it up
	// again - the only way to stop a recurring task for good. No-op (returns
	// false) if the task is unknown or already in a terminal, non-recurring
	// state.
	bool cancel(const core::String& task_id);
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
// clock read as enqueue()'s created_at_ms, just taken at parse time), and an
// optional "recurrence_ms" (a positive interval makes the task recurring -
// see Task::recurrence_ms; zero or missing means one-shot).
DROIDCLI_API bool parse_task_request_from_json(
	const core::String& json,
	Task& out_task,
	core::String& out_error);

} // namespace droidcli::app
