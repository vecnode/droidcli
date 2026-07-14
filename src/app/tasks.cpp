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
	for (Task& task : tasks_)
	{
		if (task.status == "pending")
		{
			task.status = "running";
			task.updated_at_ms = current_timestamp_ms();
			return task;
		}
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
	stream << net::json_string_field("error_message", task.error_message) << ',';
	stream << net::json_string_field("result_json", task.result_json);
	stream << '}';
	return stream.str();
}

core::String build_tasks_json(const core::Array<Task>& tasks)
{
	std::ostringstream stream;
	stream << "{\"tasks\":[";
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

	return true;
}

} // namespace droidcli::app
