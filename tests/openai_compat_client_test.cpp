#include "ai/openai_compat_client.hpp"

#include <cassert>
#include <iostream>

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
	config.temperature = 0.2f;

	assert(build_openai_chat_url(config) == "http://127.0.0.1:11434/v1/chat/completions");

	Array<ChatMessage> messages;
	messages.push_back(ChatMessage{ChatRole::System, "You are helpful."});
	messages.push_back(ChatMessage{ChatRole::User, "Say hello."});

	const OpenAICompatOutboundRequest request = build_openai_chat_request(config, messages);
	assert(request.valid);
	assert(request.url == "http://127.0.0.1:11434/v1/chat/completions");
	assert(request.body.find("\"model\":\"llama3.2\"") != String::npos);
	assert(request.body.find("\"role\":\"system\"") != String::npos);
	assert(request.body.find("Say hello.") != String::npos);
	assert(request.body.find("\"stream\":false") != String::npos);
	assert(request.body.find("\"temperature\":0.2") != String::npos);
	assert(request.body.find("\"options\":{") != String::npos);
	assert(request.body.find("\"num_ctx\":32768") != String::npos);

	// num_ctx is sent unconditionally, even with no explicit temperature
	// (temperature stays opt-in via its -1.0f sentinel; num_ctx is not).
	OpenAICompatConfig no_temperature_config = config;
	no_temperature_config.temperature = -1.0f;
	no_temperature_config.num_ctx = 8192;
	const OpenAICompatOutboundRequest no_temperature_request = build_openai_chat_request(no_temperature_config, messages);
	assert(no_temperature_request.valid);
	assert(no_temperature_request.body.find("\"num_ctx\":8192") != String::npos);
	assert(no_temperature_request.body.find("\"temperature\"") == String::npos);

	// Response parsing: OpenAI Chat Completions shape - choices[0].message.
	const OpenAICompatChatResponse ok = parse_openai_chat_response(
		200,
		R"({"id":"chatcmpl-1","model":"llama3.2","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"Hello there."},"finish_reason":"stop"}]})",
		true);
	assert(ok.http_success);
	assert(ok.done);
	assert(ok.assistant_message == "Hello there.");
	assert(ok.done_reason == "stop");
	assert(ok.thinking_text.empty());

	config.enabled = false;
	const OpenAICompatOutboundRequest disabled = build_openai_chat_request(config, messages);
	assert(!disabled.valid);
	config.enabled = true;

	// Tool-calling: request serialization includes the "tools" array, same
	// shape whichever provider is on the other end (OpenAI's function-tool
	// schema).
	Array<ToolDefinition> tools;
	ToolDefinition list_connectors_tool;
	list_connectors_tool.name = "list_connectors";
	list_connectors_tool.description = "List all registered connectors.";
	list_connectors_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{}}";
	tools.push_back(list_connectors_tool);

	const OpenAICompatOutboundRequest tool_request = build_openai_chat_request(config, messages, tools);
	assert(tool_request.valid);
	assert(tool_request.body.find("\"tools\":[") != String::npos);
	assert(tool_request.body.find("\"type\":\"function\"") != String::npos);
	assert(tool_request.body.find("\"name\":\"list_connectors\"") != String::npos);
	assert(tool_request.body.find("\"parameters\":{\"type\":\"object\"") != String::npos);

	// Request with no tools omits the "tools" field entirely.
	const OpenAICompatOutboundRequest no_tool_request = build_openai_chat_request(config, messages);
	assert(no_tool_request.body.find("\"tools\":") == String::npos);

	// Tool-calling: response parsing extracts tool_calls, decoding
	// "arguments" from a JSON-encoded STRING (the OpenAI wire convention -
	// unlike Ollama's native /api/chat, which sends arguments as a raw JSON
	// object) back into the same raw object-JSON text ToolCall::arguments_json
	// has always held.
	const OpenAICompatChatResponse tool_call_response = parse_openai_chat_response(
		200,
		R"({"id":"chatcmpl-2","model":"llama3.1","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"",)"
		R"("tool_calls":[{"id":"call_1","type":"function","function":{"name":"list_connectors",)"
		R"("arguments":"{\"foo\":\"bar\"}"}}]},"finish_reason":"tool_calls"}]})",
		true);
	assert(tool_call_response.http_success);
	assert(tool_call_response.tool_calls.size() == 1);
	assert(tool_call_response.tool_calls[0].id == "call_1");
	assert(tool_call_response.tool_calls[0].name == "list_connectors");
	assert(tool_call_response.tool_calls[0].arguments_json == "{\"foo\":\"bar\"}");

	// A response with no assistant text but a tool call is not an error.
	assert(tool_call_response.error_message.empty());

	// Thinking/reasoning: Ollama's own /v1 layer returns "reasoning" as a
	// sibling of "content" (confirmed against a real glm-4.7-flash
	// response); DeepSeek/GLM-style backends use "reasoning_content"
	// instead. Both are recognized, first match wins.
	const OpenAICompatChatResponse ollama_reasoning = parse_openai_chat_response(
		200,
		R"({"id":"chatcmpl-3","model":"glm-4.7-flash","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"408.","reasoning":"17*24 = 408."},)"
		R"("finish_reason":"stop"}]})",
		true);
	assert(ollama_reasoning.thinking_text == "17*24 = 408.");

	const OpenAICompatChatResponse deepseek_reasoning = parse_openai_chat_response(
		200,
		R"({"id":"chatcmpl-4","model":"deepseek-r1","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"408.","reasoning_content":"17*24 = 408."},)"
		R"("finish_reason":"stop"}]})",
		true);
	assert(deepseek_reasoning.thinking_text == "17*24 = 408.");

	// Telemetry: token counts from the top-level "usage" object.
	const OpenAICompatChatResponse telemetry_response = parse_openai_chat_response(
		200,
		R"({"id":"chatcmpl-5","model":"llama3.2","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"Hi."},"finish_reason":"stop"}],)"
		R"("usage":{"prompt_tokens":42,"completion_tokens":17,"total_tokens":59}})",
		true);
	assert(telemetry_response.http_success);
	assert(telemetry_response.prompt_tokens == 42);
	assert(telemetry_response.completion_tokens == 17);

	// A response with no usage field at all leaves the counts at their zero
	// default rather than erroring.
	const OpenAICompatChatResponse no_telemetry_response = parse_openai_chat_response(
		200,
		R"({"id":"chatcmpl-6","model":"llama3.2","choices":[{"index":0,)"
		R"("message":{"role":"assistant","content":"Hi."},"finish_reason":"stop"}]})",
		true);
	assert(no_telemetry_response.http_success);
	assert(no_telemetry_response.prompt_tokens == 0);
	assert(no_telemetry_response.completion_tokens == 0);

	// Tool-role message round trip: build a follow-up request carrying a tool
	// result message and confirm the wire format matches the OpenAI
	// convention.
	Array<ChatMessage> follow_up = messages;
	follow_up.push_back(ChatMessage{ChatRole::Assistant, ""});
	follow_up.push_back(ChatMessage{ChatRole::Tool, "{\"connectors\":[]}"});
	assert(chat_role_to_string(ChatRole::Tool) == "tool");
	assert(chat_role_from_string("tool") == ChatRole::Tool);

	const OpenAICompatOutboundRequest follow_up_request = build_openai_chat_request(config, follow_up);
	assert(follow_up_request.valid);
	assert(follow_up_request.body.find("\"role\":\"tool\"") != String::npos);
	// The tool message's content is a JSON string VALUE containing literal
	// JSON text ({"connectors":[]}), so json_string_field() correctly
	// escapes its embedded quotes - the wire body contains
	// \"connectors\":[] (backslash-escaped), not a bare "connectors":[]
	// substring.
	assert(follow_up_request.body.find("\\\"connectors\\\":[]") != String::npos);

	std::cout << "openai_compat_client_test passed" << std::endl;
	return 0;
}
