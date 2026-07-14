#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <cstdint>
#include <optional>

namespace metaagent::app {

struct Task {
	core::String id;
	core::String connector_id;
	core::String command;
	core::String payload_json;
	core::String status; // "pending" | "running" | "done" | "failed"
	int64_t created_at_ms = 0;
	int64_t updated_at_ms = 0;
	core::String error_message;
};

class TaskQueue {
public:
	core::String enqueue(Task task);
	std::optional<Task> claim_next();
	bool complete(const core::String& task_id);
	bool fail(const core::String& task_id, const core::String& error);
	std::optional<Task> find(const core::String& task_id) const;
	core::Array<Task> list() const;

private:
	core::Array<Task> tasks_;
	static constexpr size_t kMaxHistoryEntries = 256;

	void trim_history();
};

METAAGENT_API core::String build_task_json(const Task& task);

METAAGENT_API core::String build_tasks_json(const core::Array<Task>& tasks);

METAAGENT_API bool parse_task_request_from_json(
	const core::String& json,
	Task& out_task,
	core::String& out_error);

} // namespace metaagent::app
