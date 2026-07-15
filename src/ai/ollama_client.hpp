#pragma once

#include "ai/types.hpp"
#include "export.hpp"

// droidcli's provider seam (the Core-tier role ZeroClaw's zeroclaw-providers
// plays - see ARCHITECTURE.md's crate comparison): Ollama is the one
// concrete provider today. Request/response shaping lives here; DroidHost
// owns the actual POST via LanguageAiTransportCallbacks. A second provider
// (Anthropic/OpenAI/...) should implement the same request-build/
// response-parse shape rather than being special-cased into DroidHost -
// see the "Provider abstraction" phase in ARCHITECTURE.md's extension plan.
namespace droidcli::ai {

DROIDCLI_API core::String build_ollama_chat_url(const OllamaConfig& config);

DROIDCLI_API OllamaOutboundRequest build_ollama_chat_request(
	const OllamaConfig& config,
	const core::Array<ChatMessage>& messages,
	const core::Array<ToolDefinition>& tools = {});

DROIDCLI_API OllamaChatResponse parse_ollama_chat_response(
	int32_t status_code,
	const core::String& response_body,
	bool transport_ok);

} // namespace droidcli::ai
