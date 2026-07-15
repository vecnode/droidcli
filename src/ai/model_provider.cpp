#include "ai/model_provider.hpp"

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
	return result;
}

} // namespace droidcli::ai
