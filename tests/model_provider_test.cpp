#include "ai/model_provider.hpp"

#include <cassert>
#include <iostream>

// Exercises OllamaProvider through the ModelProvider base interface - the
// same shape DroidHost::agent_turn uses it through (cli/host.cpp) - rather
// than through OllamaProvider's own concrete type, so this test actually
// verifies the abstraction, not just the adapter's plumbing.
int main()
{
	using namespace droidcli::ai;
	using droidcli::core::Array;
	using droidcli::core::String;

	OllamaConfig config;
	config.enabled = true;
	config.base_url = "http://127.0.0.1:11434";
	config.model = "llama3.2";
	config.stream = false;

	const OllamaProvider ollama_provider(config);
	const ModelProvider& provider = ollama_provider;

	Array<ChatMessage> transcript;
	transcript.push_back(ChatMessage{ChatRole::System, "You are helpful."});
	transcript.push_back(ChatMessage{ChatRole::User, "Say hello."});

	const ProviderRequest request = provider.build_request(transcript, {});
	assert(request.valid);
	assert(request.url == "http://127.0.0.1:11434/api/chat");
	assert(request.body.find("\"model\":\"llama3.2\"") != String::npos);
	assert(request.body.find("Say hello.") != String::npos);

	const ProviderResponse response = provider.parse_response(
		200,
		R"({"model":"llama3.2","message":{"role":"assistant","content":"Hello there."},"done":true})",
		true);
	assert(response.transport_ok);
	assert(response.http_success);
	assert(response.assistant_message == "Hello there.");
	assert(response.tool_calls.empty());
	assert(response.error_message.empty());

	// A disabled config still builds an invalid request through the
	// interface - same contract build_ollama_chat_request has directly.
	OllamaConfig disabled_config = config;
	disabled_config.enabled = false;
	const OllamaProvider disabled_provider(disabled_config);
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
		R"({"model":"llama3.1","message":{"role":"assistant","content":"",)"
		R"("tool_calls":[{"id":"call_1","function":{"name":"list_connectors","arguments":{"foo":"bar"}}}]},)"
		R"("done":true})",
		true);
	assert(tool_call_response.http_success);
	assert(tool_call_response.tool_calls.size() == 1);
	assert(tool_call_response.tool_calls[0].name == "list_connectors");
	assert(tool_call_response.tool_calls[0].arguments_json == "{\"foo\":\"bar\"}");

	std::cout << "model_provider_test passed" << std::endl;
	return 0;
}
