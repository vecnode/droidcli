#include "ai/language_runtime.hpp"

#include <cassert>

int main()
{
	using namespace metaagent::ai;
	using metaagent::core::String;

	LanguageAiRuntime runtime;
	runtime.set_system_prompt("You are a concise assistant.");

	assert(runtime.submit_user_message("What is 2+2?"));
	assert(runtime.snapshot().awaiting_response);

	const OllamaOutboundRequest request = runtime.build_pending_chat_request();
	assert(request.valid);
	assert(request.body.find("What is 2+2?") != String::npos);

	OllamaChatResponse response;
	response.transport_ok = true;
	response.http_success = true;
	response.status_code = 200;
	response.done = true;
	response.assistant_message = "4";

	assert(runtime.apply_chat_response(response));
	assert(!runtime.snapshot().awaiting_response);
	assert(runtime.representation_text() == "4");
	assert(runtime.format_transcript().find("assistant: 4") != String::npos);
	assert(runtime.transcript().size() == 3);

	LanguageAiTransportCallbacks transport;
	transport.post_json = [](const String&, const String&, int32_t& status_code_out, String& response_body_out) {
		status_code_out = 200;
		response_body_out = R"({"message":{"role":"assistant","content":"Done."},"done":true})";
		return true;
	};

	assert(runtime.submit_user_message("Next question"));
	assert(runtime.complete_turn(transport));
	assert(runtime.representation_text() == "Done.");

	return 0;
}
