#pragma once

#include "ai/types.hpp"
#include "export.hpp"

// droidcli's provider seam (ZeroClaw's own Core-tier zeroclaw-providers role
// - see ARCHITECTURE.md's crate comparison; that's ZeroClaw's tier name,
// not droidcli's own module layer, where droidcli-providers lives in
// Services). Ollama is one concrete provider (see also ai/anthropic_client
// for the second). Request/response shaping lives here; DroidHost owns the
// actual POST via LanguageTransportCallbacks. A further provider (OpenAI/...)
// should implement the same request-build/response-parse shape rather than
// being special-cased into DroidHost.
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
