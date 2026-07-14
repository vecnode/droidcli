#include "app/tasks.hpp"
#include "core/types.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::app;

	TaskQueue queue;

	Task task_one;
	task_one.connector_id = "adapter-example";
	task_one.command = "summarize";
	task_one.payload_json = "{\"text\":\"hello\"}";
	const droidcli::core::String id_one = queue.enqueue(task_one);
	assert(!id_one.empty());

	Task task_two;
	task_two.command = "launch";
	const droidcli::core::String id_two = queue.enqueue(task_two);
	assert(id_two != id_one);

	assert(queue.list().size() == 2);

	auto found = queue.find(id_one);
	assert(found.has_value());
	assert(found->status == "pending");

	auto claimed = queue.claim_next();
	assert(claimed.has_value());
	assert(claimed->id == id_one);
	assert(claimed->status == "running");

	auto after_claim = queue.find(id_one);
	assert(after_claim->status == "running");

	assert(queue.complete(id_one, "{\"exit_code\":0}"));
	assert(queue.find(id_one)->status == "done");
	assert(queue.find(id_one)->result_json == "{\"exit_code\":0}");

	auto claimed_two = queue.claim_next();
	assert(claimed_two.has_value());
	assert(claimed_two->id == id_two);

	assert(queue.fail(id_two, "boom"));
	auto failed = queue.find(id_two);
	assert(failed->status == "failed");
	assert(failed->error_message == "boom");

	assert(!queue.claim_next().has_value());
	assert(!queue.complete("does-not-exist"));
	assert(!queue.fail("does-not-exist", "err"));

	const droidcli::core::String json = build_tasks_json(queue.list());
	assert(json.find("\"status\":\"done\"") != droidcli::core::String::npos);
	assert(json.find("\"status\":\"failed\"") != droidcli::core::String::npos);
	assert(json.find("\"result_json\":\"{\\\"exit_code\\\":0}\"") != droidcli::core::String::npos);

	Task parsed;
	droidcli::core::String error;
	const droidcli::core::String request_json =
		"{\"connector_id\":\"adapter-example\",\"command\":\"summarize\",\"payload_json\":\"{}\"}";
	assert(parse_task_request_from_json(request_json, parsed, error));
	assert(error.empty());
	assert(parsed.connector_id == "adapter-example");
	assert(parsed.command == "summarize");

	Task invalid;
	droidcli::core::String invalid_error;
	assert(!parse_task_request_from_json("{}", invalid, invalid_error));
	assert(!invalid_error.empty());

	std::cout << "task_queue_test passed" << std::endl;
	return 0;
}
