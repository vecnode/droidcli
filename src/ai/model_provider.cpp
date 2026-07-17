#include "ai/model_provider.hpp"

#include "ai/openai_compat_client.hpp"

namespace droidcli::ai {

OpenAICompatProvider::OpenAICompatProvider(OpenAICompatConfig config) : config_(std::move(config)) {}

ProviderRequest OpenAICompatProvider::build_request(
	const core::Array<ChatMessage>& transcript,
	const core::Array<ToolDefinition>& tools) const
{
	const OpenAICompatOutboundRequest request = build_openai_chat_request(config_, transcript, tools);

	ProviderRequest result;
	result.valid = request.valid;
	result.url = request.url;
	result.body = request.body;
	result.error_message = request.error_message;
	// No auth header for a local Ollama peer - result.headers stays empty.
	return result;
}

ProviderResponse OpenAICompatProvider::parse_response(
	const int32_t status_code,
	const core::String& response_body,
	const bool transport_ok) const
{
	const OpenAICompatChatResponse response = parse_openai_chat_response(status_code, response_body, transport_ok);

	ProviderResponse result;
	result.transport_ok = response.transport_ok;
	result.http_success = response.http_success;
	result.assistant_message = response.assistant_message;
	result.thinking_text = response.thinking_text;
	result.tool_calls = response.tool_calls;
	result.error_message = response.error_message;
	result.prompt_tokens = response.prompt_tokens;
	result.completion_tokens = response.completion_tokens;
	result.done_reason = response.done_reason;
	return result;
}

} // namespace droidcli::ai
