#include "app/tasks.hpp"

#include "net/json.hpp"

#include <atomic>
#include <chrono>
#include <sstream>

namespace droidcli::app {
namespace {

int64_t current_timestamp_ms()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

core::String generate_task_id()
{
	static std::atomic<int64_t> counter { 0 };
	const int64_t sequence = ++counter;
	std::ostringstream stream;
	stream << "task-" << current_timestamp_ms() << "-" << sequence;
	return stream.str();
}

} // namespace

core::String TaskQueue::enqueue(Task task)
{
	if (task.id.empty())
	{
		task.id = generate_task_id();
	}
	if (task.status.empty())
	{
		task.status = "pending";
	}
	const int64_t now = current_timestamp_ms();
	task.created_at_ms = now;
	task.updated_at_ms = now;

	tasks_.push_back(task);
	trim_history();
	return task.id;
}

std::optional<Task> TaskQueue::claim_next()
{
	const int64_t now = current_timestamp_ms();
	for (Task& task : tasks_)
	{
		if (task.status != "pending")
		{
			continue;
		}
		if (task.scheduled_for_ms > now)
		{
			// Not due yet - a scheduled task stays pending (and unclaimed by
			// any other pending task behind it) until the wall clock reaches
			// its deadline. tick_tasks() calling this every poll iteration
			// means it becomes claimable within one iteration of becoming due,
			// with no separate scheduler thread needed.
			continue;
		}
		task.status = "running";
		task.updated_at_ms = now;
		return task;
	}
	return std::nullopt;
}

bool TaskQueue::complete(const core::String& task_id, const core::String& result_json)
{
	for (Task& task : tasks_)
	{
		if (task.id == task_id)
		{
			task.status = "done";
			task.updated_at_ms = current_timestamp_ms();
			task.result_json = result_json;
			trim_history();
			return true;
		}
	}
	return false;
}

bool TaskQueue::fail(const core::String& task_id, const core::String& error)
{
	for (Task& task : tasks_)
	{
		if (task.id == task_id)
		{
			task.status = "failed";
			task.error_message = error;
			task.updated_at_ms = current_timestamp_ms();
			trim_history();
			return true;
		}
	}
	return false;
}

std::optional<Task> TaskQueue::find(const core::String& task_id) const
{
	for (const Task& task : tasks_)
	{
		if (task.id == task_id)
		{
			return task;
		}
	}
	return std::nullopt;
}

core::Array<Task> TaskQueue::list() const
{
	return tasks_;
}

void TaskQueue::trim_history()
{
	// Only trim completed/failed tasks, oldest first, to keep pending/running
	// entries around even if that exceeds the cap.
	while (tasks_.size() > kMaxHistoryEntries)
	{
		bool trimmed = false;
		for (auto iterator = tasks_.begin(); iterator != tasks_.end(); ++iterator)
		{
			if (iterator->status == "done" || iterator->status == "failed")
			{
				tasks_.erase(iterator);
				trimmed = true;
				break;
			}
		}
		if (!trimmed)
		{
			break;
		}
	}
}

core::String build_task_json(const Task& task)
{
	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("id", task.id) << ',';
	stream << net::json_string_field("connector_id", task.connector_id) << ',';
	stream << net::json_string_field("command", task.command) << ',';
	stream << net::json_string_field("payload_json", task.payload_json) << ',';
	stream << net::json_string_field("status", task.status) << ',';
	stream << "\"created_at_ms\":" << task.created_at_ms << ',';
	stream << "\"updated_at_ms\":" << task.updated_at_ms << ',';
	stream << "\"scheduled_for_ms\":" << task.scheduled_for_ms << ',';
	stream << net::json_string_field("error_message", task.error_message) << ',';
	stream << net::json_string_field("result_json", task.result_json);
	stream << '}';
	return stream.str();
}

core::String build_tasks_json(const core::Array<Task>& tasks)
{
	std::ostringstream stream;
	// "ok" first - see the identical comment in net::build_connectors_json;
	// this is also the list_tasks agent tool's result and needs the same
	// field for the same reason.
	stream << net::json_bool_field("ok", true) << ",\"tasks\":[";
	for (size_t index = 0; index < tasks.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		stream << build_task_json(tasks[index]);
	}
	stream << "]}";
	return stream.str();
}

bool parse_task_request_from_json(
	const core::String& json,
	Task& out_task,
	core::String& out_error)
{
	out_task = Task {};
	out_error.clear();

	out_task.command = net::extract_json_string_field(json, "command");
	if (out_task.command.empty())
	{
		out_error = "Task requires a command.";
		return false;
	}

	out_task.connector_id = net::extract_json_string_field(json, "connector_id");
	out_task.payload_json = net::extract_json_string_field(json, "payload_json");

	int64_t delay_ms = 0;
	if (net::extract_json_int_field(json, "delay_ms", delay_ms) && delay_ms > 0)
	{
		out_task.scheduled_for_ms = current_timestamp_ms() + delay_ms;
	}

	return true;
}

} // namespace droidcli::app
