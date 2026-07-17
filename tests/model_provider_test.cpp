#include "ai/model_provider.hpp"

#include <cassert>
#include <iostream>

// Exercises OpenAICompatProvider through the ModelProvider base interface -
// the same shape DroidHost::agent_turn uses it through (cli/host.cpp) -
// rather than through OpenAICompatProvider's own concrete type, so this test
// actually verifies the abstraction, not just the adapter's plumbing.
int main()
{
	using namespace droidcli::ai;
	using droidcli::core::Array;
	using droidcli::core::String;

	OpenAICompatConfig config;
	config.enabled = true;
	config.base_url = "http://127.0.0.1:11434";
	config.model = "llama3.2";
	config.stream = false;

	const OpenAICompatProvider openai_provider(config);
	const ModelProvider& provider = openai_provider;

	Array<ChatMessage> transcript;
	transcript.push_back(ChatMessage{ChatRole::System, "You are helpful."});
	transcript.push_back(ChatMessage{ChatRole::User, "Say hello."});

	const ProviderRequest request = provider.build_request(transcript, {});
	assert(request.valid);
	assert(request.url == "http://127.0.0.1:11434/v1/chat/completions");
	assert(request.body.find("\"model\":\"llama3.2\"") != String::npos);
	assert(request.body.find("Say hello.") != String::npos);

	const ProviderResponse response = provider.parse_response(
		200,
		R"({"id":"chatcmpl-1","model":"llama3.2","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"Hello there."},"finish_reason":"stop"}]})",
		true);
	assert(response.transport_ok);
	assert(response.http_success);
	assert(response.assistant_message == "Hello there.");
	assert(response.thinking_text.empty());
	assert(response.tool_calls.empty());
	assert(response.error_message.empty());

	// A disabled config still builds an invalid request through the
	// interface - same contract build_openai_chat_request has directly.
	OpenAICompatConfig disabled_config = config;
	disabled_config.enabled = false;
	const OpenAICompatProvider disabled_provider(disabled_config);
	const ProviderRequest disabled_request = disabled_provider.build_request(transcript, {});
	assert(!disabled_request.valid);
	assert(!disabled_request.error_message.empty());

	// Tool-calling round trip through the interface.
	Array<ToolDefinition> tools;
	ToolDefinition list_connectors_tool;
	list_connectors_tool.name = "list_connectors";
	list_connectors_tool.description = "List all registered connectors.";
	list_connectors_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{}}";
	tools.push_back(list_connectors_tool);

	const ProviderRequest tool_request = provider.build_request(transcript, tools);
	assert(tool_request.valid);
	assert(tool_request.body.find("\"tools\":[") != String::npos);
	assert(tool_request.body.find("\"name\":\"list_connectors\"") != String::npos);

	const ProviderResponse tool_call_response = provider.parse_response(
		200,
		R"({"id":"chatcmpl-2","model":"llama3.1","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"",)"
		R"("tool_calls":[{"id":"call_1","type":"function","function":{"name":"list_connectors",)"
		R"("arguments":"{\"foo\":\"bar\"}"}}]},"finish_reason":"tool_calls"}]})",
		true);
	assert(tool_call_response.http_success);
	assert(tool_call_response.tool_calls.size() == 1);
	assert(tool_call_response.tool_calls[0].name == "list_connectors");
	assert(tool_call_response.tool_calls[0].arguments_json == "{\"foo\":\"bar\"}");

	// Reasoning passes through the provider-agnostic interface - confirmed
	// against Ollama's own "reasoning" field naming (see
	// openai_compat_client_test.cpp for both key names it recognizes).
	// Never treated as the assistant's reply - it lands in thinking_text,
	// not assistant_message.
	const ProviderResponse thinking_response = provider.parse_response(
		200,
		R"({"id":"chatcmpl-3","model":"glm-4.7-flash","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"408.","reasoning":"17*24 = 408."},)"
		R"("finish_reason":"stop"}]})",
		true);
	assert(thinking_response.assistant_message == "408.");
	assert(thinking_response.thinking_text == "17*24 = 408.");

	// Telemetry passes through the provider-agnostic interface.
	const ProviderResponse telemetry_response = provider.parse_response(
		200,
		R"({"id":"chatcmpl-4","model":"llama3.2","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"Hi."},"finish_reason":"stop"}],)"
		R"("usage":{"prompt_tokens":42,"completion_tokens":17,"total_tokens":59}})",
		true);
	assert(telemetry_response.done_reason == "stop");
	assert(telemetry_response.prompt_tokens == 42);
	assert(telemetry_response.completion_tokens == 17);

	std::cout << "model_provider_test passed" << std::endl;
	return 0;
}
