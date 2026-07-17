#include "ai/anthropic_client.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::ai;
	using droidcli::core::Array;
	using droidcli::core::String;

	AnthropicConfig config;
	config.enabled = true;
	config.base_url = "https://api.anthropic.com";
	config.api_key = "sk-ant-test-key";
	config.model = "claude-3-5-haiku-latest";
	config.max_tokens = 1024;

	assert(build_anthropic_messages_url(config) == "https://api.anthropic.com/v1/messages");

	Array<ChatMessage> messages;
	messages.push_back(ChatMessage{ChatRole::System, "You are helpful."});
	messages.push_back(ChatMessage{ChatRole::User, "Say hello."});

	const AnthropicOutboundRequest request = build_anthropic_messages_request(config, messages);
	assert(request.valid);
	assert(request.url == "https://api.anthropic.com/v1/messages");
	assert(request.body.find("\"model\":\"claude-3-5-haiku-latest\"") != String::npos);
	assert(request.body.find("\"max_tokens\":1024") != String::npos);
	assert(request.body.find("\"role\":\"user\"") != String::npos);
	assert(request.body.find("Say hello.") != String::npos);
	// System is a top-level field, not a message in the array.
	assert(request.body.find("\"system\":\"You are helpful.\"") != String::npos);
	assert(request.body.find("\"role\":\"system\"") == String::npos);
	// Auth headers required by the wire format.
	assert(request.headers.size() == 2);
	assert(request.headers[0] == "x-api-key: sk-ant-test-key");
	assert(request.headers[1].find("anthropic-version:") != String::npos);

	// Missing API key is rejected (unlike Ollama, which needs no auth for a
	// local peer).
	AnthropicConfig no_key_config = config;
	no_key_config.api_key.clear();
	const AnthropicOutboundRequest no_key_request = build_anthropic_messages_request(no_key_config, messages);
	assert(!no_key_request.valid);
	assert(!no_key_request.error_message.empty());

	AnthropicConfig disabled_config = config;
	disabled_config.enabled = false;
	const AnthropicOutboundRequest disabled_request = build_anthropic_messages_request(disabled_config, messages);
	assert(!disabled_request.valid);

	// Tool-calling: request serialization includes "tools" with
	// "input_schema" (Anthropic's field name, not Ollama's "parameters").
	Array<ToolDefinition> tools;
	ToolDefinition list_connectors_tool;
	list_connectors_tool.name = "list_connectors";
	list_connectors_tool.description = "List all registered connectors.";
	list_connectors_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{}}";
	tools.push_back(list_connectors_tool);

	const AnthropicOutboundRequest tool_request = build_anthropic_messages_request(config, messages, tools);
	assert(tool_request.valid);
	assert(tool_request.body.find("\"tools\":[") != String::npos);
	assert(tool_request.body.find("\"input_schema\":{\"type\":\"object\"") != String::npos);
	assert(tool_request.body.find("\"name\":\"list_connectors\"") != String::npos);

	// Response parsing: plain text reply.
	const AnthropicChatResponse text_response = parse_anthropic_messages_response(
		200,
		R"({"model":"claude-3-5-haiku-latest","role":"assistant",)"
		R"("content":[{"type":"text","text":"Hello there."}],)"
		R"("stop_reason":"end_turn","usage":{"input_tokens":42,"output_tokens":17}})",
		true);
	assert(text_response.http_success);
	assert(text_response.assistant_message == "Hello there.");
	assert(text_response.tool_calls.empty());
	assert(text_response.stop_reason == "end_turn");
	assert(text_response.input_tokens == 42);
	assert(text_response.output_tokens == 17);
	assert(text_response.error_message.empty());

	// Response parsing: a tool_use content block, with raw JSON "input".
	const AnthropicChatResponse tool_call_response = parse_anthropic_messages_response(
		200,
		R"({"model":"claude-3-5-haiku-latest","role":"assistant",)"
		R"("content":[{"type":"tool_use","id":"toolu_01","name":"list_connectors","input":{"foo":"bar"}}],)"
		R"("stop_reason":"tool_use","usage":{"input_tokens":50,"output_tokens":10}})",
		true);
	assert(tool_call_response.http_success);
	assert(tool_call_response.tool_calls.size() == 1);
	assert(tool_call_response.tool_calls[0].id == "toolu_01");
	assert(tool_call_response.tool_calls[0].name == "list_connectors");
	assert(tool_call_response.tool_calls[0].arguments_json == "{\"foo\":\"bar\"}");
	assert(tool_call_response.error_message.empty());

	// A non-2xx status surfaces Anthropic's own error envelope message.
	const AnthropicChatResponse error_response = parse_anthropic_messages_response(
		401,
		R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})",
		true);
	assert(!error_response.http_success);
	assert(error_response.error_message == "invalid x-api-key");

	// A transport failure is distinguished from an HTTP-level failure.
	const AnthropicChatResponse transport_failure = parse_anthropic_messages_response(0, "", false);
	assert(!transport_failure.transport_ok);
	assert(!transport_failure.error_message.empty());

	// Role-alternation: two consecutive Tool-role transcript entries (a hop
	// with two tool calls) must merge into a single "user" message, not two
	// consecutive same-role messages - Anthropic requires strict
	// alternation. Exercised via the request body since the merge logic is
	// internal to build_anthropic_messages_request.
	Array<ChatMessage> multi_tool_transcript;
	multi_tool_transcript.push_back(ChatMessage{ChatRole::User, "Do two things."});
	multi_tool_transcript.push_back(ChatMessage{ChatRole::Assistant, ""});
	multi_tool_transcript.push_back(ChatMessage{ChatRole::Tool, "{\"a\":1}"});
	multi_tool_transcript.push_back(ChatMessage{ChatRole::Tool, "{\"b\":2}"});
	multi_tool_transcript.push_back(ChatMessage{ChatRole::Assistant, "Done with both."});
	const AnthropicOutboundRequest multi_tool_request = build_anthropic_messages_request(config, multi_tool_transcript);
	assert(multi_tool_request.valid);
	// Exactly 3 messages after merging: user, assistant, user(merged tools).
	size_t role_count = 0;
	size_t search_pos = 0;
	while ((search_pos = multi_tool_request.body.find("\"role\":", search_pos)) != String::npos)
	{
		++role_count;
		search_pos += 7;
	}
	assert(role_count == 3);
	assert(multi_tool_request.body.find("(used a tool)") != String::npos);
	assert(multi_tool_request.body.find("{\\\"a\\\":1}") != String::npos);
	assert(multi_tool_request.body.find("{\\\"b\\\":2}") != String::npos);

	std::cout << "anthropic_client_test passed" << std::endl;
	return 0;
}
