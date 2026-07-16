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

	// A task scheduled far in the future must not be claimed yet, and must
	// not block an immediately-runnable task queued behind it.
	Task scheduled;
	droidcli::core::String scheduled_error;
	const droidcli::core::String scheduled_request_json =
		"{\"command\":\"run\",\"payload_json\":\"{}\",\"delay_ms\":600000}";
	assert(parse_task_request_from_json(scheduled_request_json, scheduled, scheduled_error));
	assert(scheduled.scheduled_for_ms > 0);

	TaskQueue scheduler_queue;
	const droidcli::core::String scheduled_id = scheduler_queue.enqueue(scheduled);

	Task immediate;
	immediate.command = "run";
	const droidcli::core::String immediate_id = scheduler_queue.enqueue(immediate);

	auto next_claim = scheduler_queue.claim_next();
	assert(next_claim.has_value());
	assert(next_claim->id == immediate_id);

	assert(scheduler_queue.find(scheduled_id)->status == "pending");
	assert(!scheduler_queue.claim_next().has_value());

	// --- Recurring tasks (Phase 28) ---

	Task recurring;
	droidcli::core::String recurring_error;
	const droidcli::core::String recurring_request_json =
		"{\"command\":\"run\",\"payload_json\":\"{}\",\"recurrence_ms\":60000}";
	assert(parse_task_request_from_json(recurring_request_json, recurring, recurring_error));
	assert(recurring.recurrence_ms == 60000);

	TaskQueue recurring_queue;
	const droidcli::core::String recurring_id = recurring_queue.enqueue(recurring);
	assert(recurring_queue.find(recurring_id)->run_count == 0);

	// A successful run of a recurring task cycles back to "pending" with a
	// new scheduled_for_ms, rather than terminating in "done" the way a
	// one-shot task's complete() does above.
	auto first_run = recurring_queue.claim_next();
	assert(first_run.has_value());
	assert(first_run->id == recurring_id);
	assert(recurring_queue.find(recurring_id)->run_count == 1);
	assert(recurring_queue.complete(recurring_id, "{\"exit_code\":0}"));
	{
		const auto after_first_run = recurring_queue.find(recurring_id);
		assert(after_first_run->status == "pending");
		assert(after_first_run->scheduled_for_ms > 0);
		assert(after_first_run->result_json == "{\"exit_code\":0}");
	}
	// Rescheduled into the future - not claimable again immediately.
	assert(!recurring_queue.claim_next().has_value());

	// A *failed* run of a recurring task also cycles back to "pending"
	// (cron/SOP semantics: keep trying next time) rather than terminating in
	// "failed" the way a one-shot task's fail() does above - the error is
	// still recorded.
	Task recurring_two;
	recurring_two.command = "run";
	recurring_two.recurrence_ms = 60000;
	const droidcli::core::String recurring_two_id = recurring_queue.enqueue(recurring_two);
	auto second_claim = recurring_queue.claim_next();
	assert(second_claim.has_value());
	assert(second_claim->id == recurring_two_id);
	assert(recurring_queue.fail(recurring_two_id, "boom"));
	{
		const auto after_fail = recurring_queue.find(recurring_two_id);
		assert(after_fail->status == "pending");
		assert(after_fail->error_message == "boom");
	}

	// cancel() stops a recurring task for good - the only way to end one.
	assert(recurring_queue.cancel(recurring_id));
	assert(recurring_queue.find(recurring_id)->status == "cancelled");
	// Cancelling an already-terminal (or already-cancelled) task is a no-op.
	assert(!recurring_queue.cancel(recurring_id));
	assert(!recurring_queue.cancel("does-not-exist"));

	// A one-shot task's normal terminal behavior is unchanged by any of the
	// above - complete()/fail() on a recurrence_ms == 0 task still terminate.
	TaskQueue one_shot_queue;
	Task one_shot;
	one_shot.command = "run";
	const droidcli::core::String one_shot_id = one_shot_queue.enqueue(one_shot);
	one_shot_queue.claim_next();
	assert(one_shot_queue.complete(one_shot_id));
	assert(one_shot_queue.find(one_shot_id)->status == "done");
	assert(!one_shot_queue.cancel(one_shot_id));

	const droidcli::core::String recurring_json = build_tasks_json(recurring_queue.list());
	assert(recurring_json.find("\"recurrence_ms\":60000") != droidcli::core::String::npos);
	assert(recurring_json.find("\"run_count\":1") != droidcli::core::String::npos);
	assert(recurring_json.find("\"status\":\"cancelled\"") != droidcli::core::String::npos);

	std::cout << "task_queue_test passed" << std::endl;
	return 0;
}
