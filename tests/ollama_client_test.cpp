#include "ai/ollama_client.hpp"

#include <cassert>
#include <iostream>

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
	config.temperature = 0.2f;

	assert(build_ollama_chat_url(config) == "http://127.0.0.1:11434/api/chat");

	Array<ChatMessage> messages;
	messages.push_back(ChatMessage{ChatRole::System, "You are helpful."});
	messages.push_back(ChatMessage{ChatRole::User, "Say hello."});

	const OllamaOutboundRequest request = build_ollama_chat_request(config, messages);
	assert(request.valid);
	assert(request.url == "http://127.0.0.1:11434/api/chat");
	assert(request.body.find("\"model\":\"llama3.2\"") != String::npos);
	assert(request.body.find("\"role\":\"system\"") != String::npos);
	assert(request.body.find("Say hello.") != String::npos);
	assert(request.body.find("\"stream\":false") != String::npos);
	assert(request.body.find("\"temperature\":0.2") != String::npos);

	const OllamaChatResponse ok = parse_ollama_chat_response(
		200,
		R"({"model":"llama3.2","message":{"role":"assistant","content":"Hello there."},"done":true})",
		true);
	assert(ok.http_success);
	assert(ok.done);
	assert(ok.assistant_message == "Hello there.");

	config.enabled = false;
	const OllamaOutboundRequest disabled = build_ollama_chat_request(config, messages);
	assert(!disabled.valid);
	config.enabled = true;

	// Tool-calling: request serialization includes the "tools" array.
	Array<ToolDefinition> tools;
	ToolDefinition list_connectors_tool;
	list_connectors_tool.name = "list_connectors";
	list_connectors_tool.description = "List all registered connectors.";
	list_connectors_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{}}";
	tools.push_back(list_connectors_tool);

	const OllamaOutboundRequest tool_request = build_ollama_chat_request(config, messages, tools);
	assert(tool_request.valid);
	assert(tool_request.body.find("\"tools\":[") != String::npos);
	assert(tool_request.body.find("\"type\":\"function\"") != String::npos);
	assert(tool_request.body.find("\"name\":\"list_connectors\"") != String::npos);
	assert(tool_request.body.find("\"parameters\":{\"type\":\"object\"") != String::npos);

	// Request with no tools omits the "tools" field entirely.
	const OllamaOutboundRequest no_tool_request = build_ollama_chat_request(config, messages);
	assert(no_tool_request.body.find("\"tools\":") == String::npos);

	// Tool-calling: response parsing extracts tool_calls with raw arguments JSON.
	const OllamaChatResponse tool_call_response = parse_ollama_chat_response(
		200,
		R"({"model":"llama3.1","message":{"role":"assistant","content":"",)"
		R"("tool_calls":[{"id":"call_1","function":{"name":"list_connectors","arguments":{"foo":"bar"}}}]},)"
		R"("done":true})",
		true);
	assert(tool_call_response.http_success);
	assert(tool_call_response.tool_calls.size() == 1);
	assert(tool_call_response.tool_calls[0].id == "call_1");
	assert(tool_call_response.tool_calls[0].name == "list_connectors");
	assert(tool_call_response.tool_calls[0].arguments_json == "{\"foo\":\"bar\"}");

	// A response with no assistant text but a tool call is not an error.
	assert(tool_call_response.error_message.empty());

	// Tool-role message round trip: build a follow-up request carrying a tool
	// result message and confirm the wire format matches Ollama's convention.
	Array<ChatMessage> follow_up = messages;
	follow_up.push_back(ChatMessage{ChatRole::Assistant, ""});
	follow_up.push_back(ChatMessage{ChatRole::Tool, "{\"connectors\":[]}"});
	assert(chat_role_to_string(ChatRole::Tool) == "tool");
	assert(chat_role_from_string("tool") == ChatRole::Tool);

	const OllamaOutboundRequest follow_up_request = build_ollama_chat_request(config, follow_up);
	assert(follow_up_request.valid);
	assert(follow_up_request.body.find("\"role\":\"tool\"") != String::npos);
	// The tool message's content is a JSON string VALUE containing literal
	// JSON text ({"connectors":[]}), so json_string_field() correctly
	// escapes its embedded quotes - the wire body contains
	// \"connectors\":[] (backslash-escaped), not a bare "connectors":[]
	// substring. Searching for the unescaped form was the actual bug here,
	// not build_ollama_chat_request() - that function's escaping is
	// required by the JSON spec for embedding one JSON document inside a
	// string field of another.
	assert(follow_up_request.body.find("\\\"connectors\\\":[]") != String::npos);

	std::cout << "ollama_client_test passed" << std::endl;
	return 0;
}
