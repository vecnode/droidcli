#pragma once

#include "ai/types.hpp"
#include "export.hpp"

// droidcli's second ai::ModelProvider wire format (see "Second ModelProvider"
// in ARCHITECTURE.md) - the Anthropic Messages API. Mirrors ollama_client.hpp's
// shape (build request / parse response as free functions, no I/O of its
// own) so AnthropicProvider (model_provider.hpp) can adapt it the same way
// OllamaProvider adapts ollama_client.
namespace droidcli::ai {

DROIDCLI_API core::String build_anthropic_messages_url(const AnthropicConfig& config);

DROIDCLI_API AnthropicOutboundRequest build_anthropic_messages_request(
	const AnthropicConfig& config,
	const core::Array<ChatMessage>& messages,
	const core::Array<ToolDefinition>& tools = {});

DROIDCLI_API AnthropicChatResponse parse_anthropic_messages_response(
	int32_t status_code,
	const core::String& response_body,
	bool transport_ok);

} // namespace droidcli::ai
