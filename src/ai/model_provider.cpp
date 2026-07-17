#include "ai/model_provider.hpp"

#include "ai/anthropic_client.hpp"
#include "ai/ollama_client.hpp"

namespace droidcli::ai {

OllamaProvider::OllamaProvider(OllamaConfig config) : config_(std::move(config)) {}

ProviderRequest OllamaProvider::build_request(
	const core::Array<ChatMessage>& transcript,
	const core::Array<ToolDefinition>& tools) const
{
	const OllamaOutboundRequest request = build_ollama_chat_request(config_, transcript, tools);

	ProviderRequest result;
	result.valid = request.valid;
	result.url = request.url;
	result.body = request.body;
	result.error_message = request.error_message;
	// OllamaProvider has no headers to add - result.headers stays empty.
	return result;
}

ProviderResponse OllamaProvider::parse_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok) const
{
	const OllamaChatResponse response = parse_ollama_chat_response(status_code, response_body, transport_ok);

	ProviderResponse result;
	result.transport_ok = response.transport_ok;
	result.http_success = response.http_success;
	result.assistant_message = response.assistant_message;
	result.tool_calls = response.tool_calls;
	result.error_message = response.error_message;
	result.total_duration_ms = response.total_duration_ns / 1'000'000;
	result.eval_duration_ms = response.eval_duration_ns / 1'000'000;
	result.prompt_tokens = response.prompt_eval_count;
	result.completion_tokens = response.eval_count;
	result.done_reason = response.done_reason;
	return result;
}

AnthropicProvider::AnthropicProvider(AnthropicConfig config) : config_(std::move(config)) {}

ProviderRequest AnthropicProvider::build_request(
	const core::Array<ChatMessage>& transcript,
	const core::Array<ToolDefinition>& tools) const
{
	const AnthropicOutboundRequest request = build_anthropic_messages_request(config_, transcript, tools);

	ProviderRequest result;
	result.valid = request.valid;
	result.url = request.url;
	result.body = request.body;
	result.error_message = request.error_message;
	result.headers = request.headers;
	return result;
}

ProviderResponse AnthropicProvider::parse_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok) const
{
	const AnthropicChatResponse response = parse_anthropic_messages_response(status_code, response_body, transport_ok);

	ProviderResponse result;
	result.transport_ok = response.transport_ok;
	result.http_success = response.http_success;
	result.assistant_message = response.assistant_message;
	result.tool_calls = response.tool_calls;
	result.error_message = response.error_message;
	// Anthropic reports no per-call duration on the wire, unlike Ollama -
	// total_duration_ms/eval_duration_ms stay at their zero default.
	result.prompt_tokens = response.input_tokens;
	result.completion_tokens = response.output_tokens;
	result.done_reason = response.stop_reason;
	return result;
}

} // namespace droidcli::ai
